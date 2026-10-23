// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp: contained quality test. A Windows program (run under Proton) that feeds frames dumped by the "dump" upscaler
// plugin (plugins/dump) through AMD FidelityFX Super Resolution 4 (the FSR API's DX12 upscaler) and writes the result.
// It uses AMD's signed DLLs from the FidelityFX SDK, which are fetched locally and never committed.
//
//   fsr4_runner.exe <dump dir> <out dir> [version-substring] [linear]   (default: the newest provider, e.g. "4.1.1")
// Reads frame_NNNN.bin, writes fsr4_NNNN.raw (RGBA16F, output size) and log.txt in <out dir>.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ffx_api_loader.h"
#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"

static FILE* g_log = nullptr;
#define LOG(...) do { fprintf(g_log, __VA_ARGS__); fflush(g_log); } while (0)

struct Hdr {
    uint32_t magic, version, rw, rh, ow, oh;
    float jitter_x, jitter_y, camera_near, camera_far, camera_fov_y, sharpness, delta_ms;
    uint32_t reset, color_format, depth_format;
    uint64_t frame_index;
};
static_assert(sizeof(Hdr) == 72, "header layout");

static void FfxMessage(uint32_t type, const wchar_t* message) { LOG("[ffx %u] %ls\n", type, message); }

#define CHECK(expr) do { HRESULT _h = (expr); if (FAILED(_h)) { LOG("FAILED %s -> 0x%08lx\n", #expr, (unsigned long)_h); return 1; } } while (0)

static ID3D12Resource* MakeTex(ID3D12Device* dev, DXGI_FORMAT fmt, UINT w, UINT h, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}

static ID3D12Resource* MakeBuf(ID3D12Device* dev, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{type};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    return r;
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string in_dir = argv[1], out_dir = argv[2];
    const std::string want_version = argc > 3 ? argv[3] : "";
    const bool assume_linear = argc > 4 && std::string(argv[4]) == "linear"; // test: pass the colour as if it were linear
    CreateDirectoryA(out_dir.c_str(), nullptr);
    g_log = fopen((out_dir + "\\log.txt").c_str(), "w");

    HMODULE loader = LoadLibraryA("amd_fidelityfx_loader_dx12.dll");
    if (!loader) { LOG("cannot load amd_fidelityfx_loader_dx12.dll (%lu)\n", GetLastError()); return 1; }
    ffxFunctions fn{};
    ffxLoadFunctions(&fn, loader);

    IDXGIFactory4* factory = nullptr;
    CHECK(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    IDXGIAdapter1* adapter = nullptr;
    CHECK(factory->EnumAdapters1(0, &adapter));
    DXGI_ADAPTER_DESC1 ad;
    adapter->GetDesc1(&ad);
    LOG("adapter %ls\n", ad.Description);
    ID3D12Device* dev = nullptr;
    CHECK(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev)));
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandQueue* queue = nullptr;
    CHECK(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    ID3D12CommandAllocator* alloc = nullptr;
    CHECK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
    ID3D12GraphicsCommandList* list = nullptr;
    CHECK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list)));
    list->Close();
    ID3D12Fence* fence = nullptr;
    CHECK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    UINT64 fence_value = 0;
    auto wait = [&]() {
        queue->Signal(fence, ++fence_value);
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }
    };

    // Versions the FSR API offers for the upscaler on this device.
    uint64_t version_count = 0;
    ffxQueryDescGetVersions qv{};
    qv.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    qv.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    qv.device = dev;
    qv.outputCount = &version_count;
    ffxReturnCode_t rc = fn.Query(nullptr, &qv.header);
    LOG("version query: rc %u, %llu versions\n", rc, (unsigned long long)version_count);
    std::vector<uint64_t> ids(version_count ? version_count : 1);
    std::vector<const char*> names(version_count ? version_count : 1);
    uint64_t cap = version_count;
    qv.outputCount = &cap;
    qv.versionIds = ids.data();
    qv.versionNames = names.data();
    rc = fn.Query(nullptr, &qv.header);
    uint64_t chosen = 0;
    for (uint64_t i = 0; i < cap; ++i) {
        LOG("  version %llu: id %llu '%s'\n", (unsigned long long)i, (unsigned long long)ids[i], names[i]);
        if (!want_version.empty() && std::string(names[i]).find(want_version) != std::string::npos) chosen = ids[i];
    }
    if (want_version.empty() && cap) chosen = ids[0]; // the list is newest first
    LOG("chosen version id %llu\n", (unsigned long long)chosen);

    // Read the first frame for the sizes.
    char path[512];
    snprintf(path, sizeof(path), "%s\\frame_0000.bin", in_dir.c_str());
    FILE* f0 = fopen(path, "rb");
    if (!f0) { LOG("no frames in %s\n", in_dir.c_str()); return 1; }
    Hdr h0{};
    fread(&h0, sizeof(h0), 1, f0);
    fclose(f0);
    const UINT rw = h0.rw, rh = h0.rh, ow = h0.ow, oh = h0.oh;

    ffxCreateContextDescUpscale cu{};
    cu.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    cu.flags = FFX_UPSCALE_ENABLE_DEPTH_INFINITE | (assume_linear ? 0 : FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE);
    cu.maxRenderSize = {rw, rh};
    cu.maxUpscaleSize = {ow, oh};
    cu.fpMessage = &FfxMessage;
    ffxCreateBackendDX12Desc cb{};
    cb.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    cb.device = dev;
    ffxOverrideVersion ov{};
    ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    ov.versionId = chosen;
    cu.header.pNext = &cb.header;
    cb.header.pNext = chosen ? &ov.header : nullptr;
    ffxContext ctx = nullptr;
    rc = fn.CreateContext(&ctx, &cu.header, nullptr);
    LOG("create context: rc %u\n", rc);
    if (rc != FFX_API_RETURN_OK) return 1;
    ffxQueryGetProviderVersion pv{};
    pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (fn.Query(&ctx, &pv.header) == FFX_API_RETURN_OK) LOG("provider: %s\n", pv.versionName ? pv.versionName : "?");

    // Textures: colour R10G10B10A2 (the Vulkan A2B10G10R10 pack has R in the low bits, the same layout), depth R32F, MV RG16F.
    ID3D12Resource* color = MakeTex(dev, DXGI_FORMAT_R10G10B10A2_UNORM, rw, rh, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* depth = MakeTex(dev, DXGI_FORMAT_R32_FLOAT, rw, rh, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* motion = MakeTex(dev, DXGI_FORMAT_R16G16_FLOAT, rw, rh, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* output = MakeTex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, ow, oh, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!color || !depth || !motion || !output) { LOG("texture creation failed\n"); return 1; }

    auto footprint = [&](ID3D12Resource* r, UINT64* total, UINT* rows, UINT64* row_size) {
        D3D12_RESOURCE_DESC d = r->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, rows, row_size, total);
        return fp;
    };
    UINT64 tot_c, tot_d, tot_m, tot_o, rs_c, rs_d, rs_m, rs_o;
    UINT rows_c, rows_d, rows_m, rows_o;
    auto fp_c = footprint(color, &tot_c, &rows_c, &rs_c);
    auto fp_d = footprint(depth, &tot_d, &rows_d, &rs_d);
    auto fp_m = footprint(motion, &tot_m, &rows_m, &rs_m);
    auto fp_o = footprint(output, &tot_o, &rows_o, &rs_o);
    ID3D12Resource* up_c = MakeBuf(dev, tot_c, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource* up_d = MakeBuf(dev, tot_d, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource* up_m = MakeBuf(dev, tot_m, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource* rb = MakeBuf(dev, tot_o, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    if (!up_c || !up_d || !up_m || !rb) { LOG("buffer creation failed\n"); return 1; }

    auto barrier = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER bar{};
        bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
        list->ResourceBarrier(1, &bar);
    };
    auto fill = [&](ID3D12Resource* up, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp, UINT rows, UINT64 row_size, const uint8_t* src, size_t src_pitch) {
        uint8_t* p = nullptr;
        up->Map(0, nullptr, (void**)&p);
        for (UINT y = 0; y < rows; ++y) memcpy(p + fp.Offset + (UINT64)y * fp.Footprint.RowPitch, src + y * src_pitch, row_size);
        up->Unmap(0, nullptr);
    };
    auto copy_in = [&](ID3D12Resource* dst, ID3D12Resource* up, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp) {
        D3D12_TEXTURE_COPY_LOCATION d{dst, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        d.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION s{up, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        s.PlacedFootprint = fp;
        list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    };

    std::vector<uint8_t> buf_c(rw * rh * 4), buf_d(rw * rh * 4), buf_m(rw * rh * 4);
    int frames_done = 0;
    for (int n = 0;; ++n) {
        snprintf(path, sizeof(path), "%s\\frame_%04d.bin", in_dir.c_str(), n);
        FILE* f = fopen(path, "rb");
        if (!f) break;
        Hdr h{};
        fread(&h, sizeof(h), 1, f);
        fread(buf_c.data(), 1, buf_c.size(), f);
        fread(buf_d.data(), 1, buf_d.size(), f);
        fread(buf_m.data(), 1, buf_m.size(), f);
        fclose(f);
        fill(up_c, fp_c, rows_c, rs_c, buf_c.data(), rw * 4);
        fill(up_d, fp_d, rows_d, rs_d, buf_d.data(), rw * 4);
        fill(up_m, fp_m, rows_m, rs_m, buf_m.data(), rw * 4);

        alloc->Reset();
        list->Reset(alloc, nullptr);
        copy_in(color, up_c, fp_c);
        copy_in(depth, up_d, fp_d);
        copy_in(motion, up_m, fp_m);
        barrier(color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(motion, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ffxDispatchDescUpscale du{};
        du.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        du.commandList = list;
        du.color = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        du.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        du.motionVectors = ffxApiGetResourceDX12(motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        du.output = ffxApiGetResourceDX12(output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        du.jitterOffset = {h.jitter_x, h.jitter_y};
        du.motionVectorScale = {1.0f, 1.0f};
        du.renderSize = {h.rw, h.rh};
        du.upscaleSize = {h.ow, h.oh};
        du.enableSharpening = false; // judge the upscaler itself
        du.sharpness = 0.0f;
        du.frameTimeDelta = h.delta_ms;
        du.preExposure = 1.0f;
        du.reset = (n == 0) || h.reset != 0;
        du.cameraNear = h.camera_near > 0 ? h.camera_near : 2.0f;
        du.cameraFar = 3.402823466e+38f; // infinite far plane
        du.cameraFovAngleVertical = h.camera_fov_y > 0 ? h.camera_fov_y : 0.97f;
        du.viewSpaceToMetersFactor = 0.01f;
        du.flags = assume_linear ? 0 : FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        rc = fn.Dispatch(&ctx, &du.header);
        if (rc != FFX_API_RETURN_OK) { LOG("frame %d: dispatch rc %u\n", n, rc); return 1; }

        barrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION s{output, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        s.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION d{rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        d.PlacedFootprint = fp_o;
        list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        barrier(output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        barrier(motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        list->Close();
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        wait();

        uint8_t* p = nullptr;
        rb->Map(0, nullptr, (void**)&p);
        snprintf(path, sizeof(path), "%s\\fsr4_%04d.raw", out_dir.c_str(), n);
        FILE* fo = fopen(path, "wb");
        for (UINT y = 0; y < rows_o; ++y) fwrite(p + fp_o.Offset + (UINT64)y * fp_o.Footprint.RowPitch, 1, rs_o, fo);
        fclose(fo);
        rb->Unmap(0, nullptr);
        ++frames_done;
        if (n % 20 == 0) LOG("frame %d done\n", n);
    }
    LOG("done: %d frames\n", frames_done);
    fn.DestroyContext(&ctx, nullptr);
    return 0;
}
