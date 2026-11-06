// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp upscaler plugin: AMD FSR 4 (ML upscaling) through the AMD FSR API of the FidelityFX SDK 2.4+, as kr_upscaler_fsr4.dll.
//
// AMD ships FSR 4 only as signed DLLs (amd_fidelityfx_loader_vk.dll + amd_fidelityfx_upscaler_vk.dll, redistributable in binary form).
// This plugin contains none of AMD's code: it loads the loader DLL that sits next to it and talks to it through the public headers
// (Kits/FidelityFX/api/include), which are only needed to build it. Nothing here is linked against AMD's libraries.
//
// Which model runs is decided by the SDK from the GPU: the full FP8 model on Radeon RX 9000, the INT8 model on RX 7000 / RX 6000 and
// Ryzen AI Max, the lightweight INT8 model on Ryzen AI 9 / Z2 handhelds, and FSR 3.1 as the SDK's own fallback on any other GPU.
// "Auto" leaves that to the SDK (and to the driver, which may carry a newer version); the menu's Model row can pin one.
//
// Frame generation (FSR 4's ML frame generation, SDK 2.4, Vulkan; needs a Radeon RX 9000 and Windows 11) is the same library's second backend, exported
// as kr_framegen_get_api and found by the host through the kr_framegen_fsr4.dll forwarder. It is driven without AMD's swapchain wrapper: per game frame the
// host hands over the HUD-free display-referred 3D image, the depth and the motion vectors it derived (camera from depth + view-projection, plus the
// per-object matching layer), and this backend runs ffxConfigure, the "prepare" dispatch (depth, motion vectors, camera basis) and the generation dispatch
// into the host's output image; the host then lays the HUD over it and presents it between the real frames.
//
// Colour: the host's 3D image is tonemapped and display-encoded (sRGB), so the context is created with the non-linear colour
// flags and the colour goes in as it is. Motion vectors are in render pixels (prev = cur + mv); depth is the scene depth.
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define KR_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#define KR_EXPORT __attribute__((visibility("default")))
#endif

#include <vulkan/vulkan.h>

#include <vk/ffx_api_vk.h>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <ffx_framegeneration.h>

#include "kr_upscaler_abi.h"

namespace {

// ------------------------------------------------------------------------------------------------ the loader DLL
struct Api {
    PfnFfxCreateContext create = nullptr;
    PfnFfxDestroyContext destroy = nullptr;
    PfnFfxDispatch dispatch = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxConfigure configure = nullptr;
    bool tried = false;
    bool ok = false;
};
Api g_api;

#ifdef _WIN32
std::wstring PluginDir() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&PluginDir), &self)) {
        return L".";
    }
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(self, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return L".";
    std::wstring dir(buf, n);
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : dir.substr(0, slash);
}

bool LoadApi() {
    if (g_api.tried) return g_api.ok;
    g_api.tried = true;
    // The loader finds amd_fidelityfx_upscaler_vk.dll next to itself (SDK 2.4), so both live in this plugin's folder.
    const std::wstring path = PluginDir() + L"\\amd_fidelityfx_loader_vk.dll";
    HMODULE lib = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!lib) {
        std::fprintf(stderr, "[fsr4] cannot load amd_fidelityfx_loader_vk.dll next to the plugin (Windows error %lu)\n", GetLastError());
        return false;
    }
    g_api.create = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(lib, "ffxCreateContext"));
    g_api.destroy = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(lib, "ffxDestroyContext"));
    g_api.dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(lib, "ffxDispatch"));
    g_api.query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(lib, "ffxQuery"));
    g_api.configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(lib, "ffxConfigure"));
    g_api.ok = g_api.create && g_api.destroy && g_api.dispatch && g_api.query && g_api.configure;
    if (!g_api.ok) std::fprintf(stderr, "[fsr4] the AMD FSR loader DLL lacks the expected functions\n");
    return g_api.ok;
}
#else
bool LoadApi() { // the DLLs are Windows binaries; this build exists only so the source can be checked on Linux
    g_api.tried = true;
    return false;
}
#endif

void Message(uint32_t type, const wchar_t* text) {
    std::fwprintf(stderr, L"[fsr4] %ls: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? L"error" : L"warning", text ? text : L"");
}

// ------------------------------------------------------------------------------------------------ models
struct Version {
    uint64_t id;
    std::string name;
};

int g_model = 0;                  // 0 = Auto, else 1 + index into the versions
std::vector<Version> g_versions;  // filled by the first context (needs the device)
std::string g_active;

struct Fsr4 {
    KrUpscalerDevice dev{};
    VkDeviceContext devctx{};
    PFN_vkCmdPipelineBarrier barrier = nullptr;
    ffxContext ctx = nullptr;
    uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    uint32_t flags = 0;
    int model_used = -1;
    std::vector<const char*> name_ptrs; // for kr_upscaler_models
};
Fsr4* g_current = nullptr;

void QueryVersions(Fsr4* f) {
    uint64_t count = 0;
    ffxQueryDescGetVersions q{};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    q.device = &f->devctx;
    q.outputCount = &count;
    if (g_api.query(nullptr, &q.header) != FFX_API_RETURN_OK || count == 0) return;
    std::vector<uint64_t> ids(count);
    std::vector<const char*> names(count);
    q.outputCount = &count;
    q.versionIds = ids.data();
    q.versionNames = names.data();
    if (g_api.query(nullptr, &q.header) != FFX_API_RETURN_OK) return;
    g_versions.clear();
    for (uint64_t i = 0; i < count; ++i) g_versions.push_back({ids[i], names[i] ? names[i] : "?"});
}

void DestroyContext(Fsr4* f) {
    if (f->ctx) {
        g_api.destroy(&f->ctx, nullptr);
        f->ctx = nullptr;
    }
}

bool MakeContext(Fsr4* f, uint32_t depth_inverted) {
    DestroyContext(f);
    f->flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE | FFX_UPSCALE_ENABLE_DEPTH_INFINITE |
               (depth_inverted ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0);
    if (std::getenv("KARTRECOMP_FSR4_DEBUG")) f->flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;

    for (int attempt = 0; attempt < 2; ++attempt) {
        ffxCreateBackendVKDesc backend{};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backend.deviceContext = f->devctx;
        ffxCreateContextDescUpscaleVersion version{};
        version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
        version.version = FFX_UPSCALER_VERSION;
        ffxOverrideVersion pin{};
        pin.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        ffxCreateContextDescUpscale up{};
        up.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        up.flags = f->flags;
        up.maxRenderSize = {f->rw, f->rh};
        up.maxUpscaleSize = {f->ow, f->oh};
        up.fpMessage = &Message;
        up.header.pNext = &backend.header;
        backend.header.pNext = &version.header;
        const bool pinned = attempt == 0 && g_model > 0 && static_cast<size_t>(g_model) <= g_versions.size();
        if (pinned) {
            pin.versionId = g_versions[static_cast<size_t>(g_model) - 1].id;
            version.header.pNext = &pin.header;
        }
        const ffxReturnCode_t r = g_api.create(&f->ctx, &up.header, nullptr);
        if (r == FFX_API_RETURN_OK) {
            f->model_used = pinned ? g_model : 0;
            ffxQueryGetProviderVersion pv{};
            pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
            g_active.clear();
            if (g_api.query(&f->ctx, &pv.header) == FFX_API_RETURN_OK && pv.versionName) g_active = pv.versionName;
            return true;
        }
        f->ctx = nullptr;
        if (!pinned) {
            std::fprintf(stderr, "[fsr4] ffxCreateContext failed (%u)\n", r);
            return false;
        }
        std::fprintf(stderr, "[fsr4] the chosen model cannot run on this GPU (%u); using Auto\n", r);
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ the plugin
void* Create(const KrUpscalerDevice* device) {
    if (!device || !device->device || !device->get_device_proc_addr) return nullptr; // no GPU instance: nothing to create
    if (!LoadApi()) return nullptr;
    auto* f = new Fsr4;
    f->dev = *device;
    f->devctx.vkDevice = device->device;
    f->devctx.vkPhysicalDevice = device->physical;
    f->devctx.vkDeviceProcAddr = device->get_device_proc_addr;
    f->barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->get_device_proc_addr(device->device, "vkCmdPipelineBarrier"));
    if (g_versions.empty()) QueryVersions(f);
    if (g_versions.empty()) {
        std::fprintf(stderr, "[fsr4] the AMD FSR runtime offers no upscaler for this device\n");
        delete f;
        return nullptr;
    }
    g_current = f;
    return f;
}

int Configure(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat) {
    auto* f = static_cast<Fsr4*>(instance);
    if (f->ctx && f->rw == render_w && f->rh == render_h && f->ow == out_w && f->oh == out_h) return 0;
    f->rw = render_w;
    f->rh = render_h;
    f->ow = out_w;
    f->oh = out_h;
    DestroyContext(f); // created in Dispatch, where the depth convention of the frame is known
    return 0;
}

FfxApiResource Image(VkImage image, VkFormat format, uint32_t w, uint32_t h, bool storage, uint32_t state, bool depth = false) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0) |
               (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0);
    return ffxApiGetResourceVK(reinterpret_cast<void*>(image), ffxApiGetImageResourceDescriptionVK(image, ci, 0), state);
}

void ToGeneral(Fsr4* f, VkCommandBuffer cmd, const std::vector<std::pair<VkImage, VkImageAspectFlags>>& images) {
    std::vector<VkImageMemoryBarrier> b;
    for (const auto& [image, aspect] : images) {
        VkImageMemoryBarrier m{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        m.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        m.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        m.image = image;
        m.subresourceRange = {aspect, 0, 1, 0, 1};
        b.push_back(m);
    }
    f->barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
               static_cast<uint32_t>(b.size()), b.data());
}

void Dispatch(void* instance, const KrUpscalerFrame* fr) {
    auto* f = static_cast<Fsr4*>(instance);
    if (!f->rw || !fr->depth || !fr->motion) return;
    if (!f->ctx || f->model_used != (g_model > 0 && static_cast<size_t>(g_model) <= g_versions.size() ? g_model : 0) ||
        ((f->flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) != 0) != (fr->depth_inverted != 0)) {
        if (!MakeContext(f, fr->depth_inverted)) {
            f->rw = 0; // do not retry every frame
            return;
        }
    }
    const VkFormat depth_format = fr->depth_format != VK_FORMAT_UNDEFINED ? fr->depth_format : VK_FORMAT_D32_SFLOAT;
    // The host keeps everything in GENERAL. Inputs are declared read-only (the runtime moves them to the layout it samples from and
    // leaves them there; they go back to GENERAL below), the output as a storage image.
    constexpr uint32_t kRead = FFX_API_RESOURCE_STATE_COMPUTE_READ;
    constexpr uint32_t kWrite = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
    const bool have_reactive = fr->mismatch_mask != VK_NULL_HANDLE && !std::getenv("KARTRECOMP_FSR4_NOREACTIVE");

    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = fr->cmd;
    d.color = Image(fr->color, fr->color_format, fr->render_width, fr->render_height, false, kRead);
    d.depth = Image(fr->depth, depth_format, fr->render_width, fr->render_height, false, kRead, true);
    d.motionVectors = Image(fr->motion, VK_FORMAT_R16G16_SFLOAT, fr->render_width, fr->render_height, false, kRead);
    if (have_reactive) d.reactive = Image(fr->mismatch_mask, VK_FORMAT_R8_UNORM, fr->render_width, fr->render_height, false, kRead);
    d.output = Image(fr->output, fr->color_format, fr->output_width, fr->output_height, true, kWrite);
    d.jitterOffset = {fr->jitter_x, fr->jitter_y};
    d.motionVectorScale = {1.0f, 1.0f}; // already in render pixels
    d.renderSize = {fr->render_width, fr->render_height};
    d.upscaleSize = {fr->output_width, fr->output_height};
    d.enableSharpening = fr->sharpness > 0.0f;
    d.sharpness = fr->sharpness;
    d.frameTimeDelta = fr->delta_time_ms > 0.0f ? fr->delta_time_ms : 16.6f;
    d.preExposure = fr->exposure > 0.0f ? fr->exposure : 1.0f;
    d.reset = fr->reset != 0;
    d.cameraNear = fr->camera_near > 0.0f ? fr->camera_near : 2.0f;
    d.cameraFar = FLT_MAX; // infinite far plane (FFX_UPSCALE_ENABLE_DEPTH_INFINITE)
    d.cameraFovAngleVertical = fr->camera_fov_y > 0.0f ? fr->camera_fov_y : 0.97f;
    d.viewSpaceToMetersFactor = 0.01f; // the game's units are centimetres
    d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

    const ffxReturnCode_t r = g_api.dispatch(&f->ctx, &d.header);
    if (r != FFX_API_RETURN_OK) {
        std::fprintf(stderr, "[fsr4] dispatch failed (%u): render %ux%u -> %ux%u\n", r, fr->render_width, fr->render_height,
                     fr->output_width, fr->output_height);
        return;
    }
    std::vector<std::pair<VkImage, VkImageAspectFlags>> back = {
        {fr->color, VK_IMAGE_ASPECT_COLOR_BIT},
        {fr->motion, VK_IMAGE_ASPECT_COLOR_BIT},
        {fr->depth, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT |
             ((depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))},
    };
    if (have_reactive) back.push_back({fr->mismatch_mask, VK_IMAGE_ASPECT_COLOR_BIT});
    ToGeneral(f, fr->cmd, back);
}

void Destroy(void* instance) {
    auto* f = static_cast<Fsr4*>(instance);
    if (g_current == f) g_current = nullptr;
    DestroyContext(f);
    delete f;
}

const KrUpscalerApi kApi = {KR_UPSCALER_ABI_VERSION, "FSR 4", &Create, &Configure, &Dispatch, &Destroy};

// ------------------------------------------------------------------------------------------------ frame generation
struct Fg {
    KrUpscalerDevice dev{};
    VkDeviceContext devctx{};
    PFN_vkCmdPipelineBarrier barrier = nullptr;
    ffxContext ctx = nullptr;
    uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    uint32_t flags = 0;
    uint64_t frame_id = 1;
};

// Camera basis and position from a view-projection (rows: clip = row . (world, 1); x right, y up, w = depth along the view direction).
// right = grad(clip.x), up = grad(clip.y), forward = grad(clip.w); the position is where x = y = w = 0.
bool CameraFromViewProj(const float* m, float pos[3], float up[3], float right[3], float fwd[3]) {
    const float* r0 = m;       // x
    const float* r1 = m + 4;   // y
    const float* r3 = m + 12;  // w
    const auto norm = [](const float* v, float* out) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l < 1e-12f) return false;
        for (int i = 0; i < 3; ++i) out[i] = v[i] / l;
        return true;
    };
    if (!norm(r0, right) || !norm(r1, up) || !norm(r3, fwd)) return false;
    // Solve [r0.xyz; r1.xyz; r3.xyz] * p = -[r0.w, r1.w, r3.w] (Cramer's rule).
    const float a[3][3] = {{r0[0], r0[1], r0[2]}, {r1[0], r1[1], r1[2]}, {r3[0], r3[1], r3[2]}};
    const float b[3] = {-r0[3], -r1[3], -r3[3]};
    const auto det = [](const float c[3][3]) {
        return c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) +
               c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
    };
    const float d = det(a);
    if (std::fabs(d) < 1e-20f) return false;
    for (int k = 0; k < 3; ++k) {
        float t[3][3];
        std::memcpy(t, a, sizeof(t));
        for (int i = 0; i < 3; ++i) t[i][k] = b[i];
        pos[k] = det(t) / d;
    }
    return true;
}

void DestroyFg(Fg* g) {
    if (g->ctx) {
        g_api.destroy(&g->ctx, nullptr);
        g->ctx = nullptr;
    }
}

void* FgCreate(const KrUpscalerDevice* device) {
    if (!device || !device->device || !device->get_device_proc_addr) return nullptr; // no GPU instance: nothing to create
    if (!LoadApi()) return nullptr;
    auto* g = new Fg;
    g->dev = *device;
    g->devctx.vkDevice = device->device;
    g->devctx.vkPhysicalDevice = device->physical;
    g->devctx.vkDeviceProcAddr = device->get_device_proc_addr;
    g->barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->get_device_proc_addr(device->device, "vkCmdPipelineBarrier"));
    uint64_t count = 0;
    ffxQueryDescGetVersions q{};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    q.device = &g->devctx;
    q.outputCount = &count;
    if (g_api.query(nullptr, &q.header) != FFX_API_RETURN_OK || count == 0) {
        std::fprintf(stderr, "[fsr4-fg] the AMD FSR runtime has no frame generation for this device\n");
        delete g;
        return nullptr;
    }
    return g;
}

int FgConfigure(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat color_format) {
    auto* g = static_cast<Fg*>(instance);
    if (g->ctx && g->rw == render_w && g->rh == render_h && g->ow == out_w && g->oh == out_h && g->fmt == color_format) return 0;
    DestroyFg(g);
    g->rw = render_w;
    g->rh = render_h;
    g->ow = out_w;
    g->oh = out_h;
    g->fmt = color_format;
    g->flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
    if (std::getenv("KARTRECOMP_FSR4_DEBUG")) g->flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
    ffxCreateBackendVKDesc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.deviceContext = g->devctx;
    ffxCreateContextDescFrameGenerationVersion version{};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    version.version = FFX_FRAMEGENERATION_VERSION;
    ffxCreateContextDescFrameGeneration fg{};
    fg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    fg.flags = g->flags;
    fg.displaySize = {out_w, out_h};
    fg.maxRenderSize = {render_w, render_h};
    fg.backBufferFormat = ffxApiGetSurfaceFormatVK(color_format);
    fg.header.pNext = &backend.header;
    backend.header.pNext = &version.header;
    const ffxReturnCode_t r = g_api.create(&g->ctx, &fg.header, nullptr);
    if (r != FFX_API_RETURN_OK) {
        std::fprintf(stderr, "[fsr4-fg] ffxCreateContext failed (%u): FSR 4 frame generation needs a Radeon RX 9000 and the Vulkan runtime of Adrenalin 26.10.1 or newer\n", r);
        g->ctx = nullptr;
        return 1;
    }
    g->frame_id += 2; // a fresh context starts a new sequence
    return 0;
}

int FgDispatch(void* instance, const KrFrameGenFrame* fr) {
    auto* g = static_cast<Fg*>(instance);
    if (!g->ctx || !fr->cur_color || !fr->output || !fr->depth || !fr->motion) return 1;
    const VkFormat depth_format = fr->depth_format != VK_FORMAT_UNDEFINED ? fr->depth_format : VK_FORMAT_D32_SFLOAT;
    constexpr uint32_t kRead = FFX_API_RESOURCE_STATE_COMPUTE_READ;
    constexpr uint32_t kWrite = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;
    // One more than the last frame, or two on a cut: any other step tells the runtime to drop its history.
    g->frame_id += fr->reset ? 2 : 1;

    ffxConfigureDescFrameGeneration cfg{};
    cfg.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    cfg.swapChain = nullptr;  // no AMD swapchain wrapper: the host presents
    cfg.frameGenerationEnabled = true;
    cfg.allowAsyncWorkloads = false;
    cfg.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    cfg.generationRect = {0, 0, static_cast<int32_t>(fr->output_width), static_cast<int32_t>(fr->output_height)};
    cfg.frameID = g->frame_id;
    ffxReturnCode_t r = g_api.configure ? g_api.configure(&g->ctx, &cfg.header) : FFX_API_RETURN_ERROR;
    if (r != FFX_API_RETURN_OK) {
        static bool once = false;
        if (!once) std::fprintf(stderr, "[fsr4-fg] ffxConfigure failed (%u)\n", r);
        once = true;
        return 1;
    }

    ffxDispatchDescFrameGenerationPrepareV2 prep{};
    prep.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prep.frameID = g->frame_id;
    prep.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    prep.commandList = fr->cmd;
    prep.renderSize = {fr->render_width, fr->render_height};
    prep.jitterOffset = {fr->jitter_x, fr->jitter_y};
    prep.motionVectorScale = {1.0f, 1.0f}; // render pixels
    prep.frameTimeDelta = fr->delta_time_ms > 0.0f ? fr->delta_time_ms : 16.6f;
    prep.reset = fr->reset != 0;
    prep.cameraNear = fr->camera_near > 0.0f ? fr->camera_near : 2.0f;
    prep.cameraFar = FLT_MAX;
    prep.cameraFovAngleVertical = fr->camera_fov_y > 0.0f ? fr->camera_fov_y : 0.97f;
    prep.viewSpaceToMetersFactor = 0.01f; // the game's units are centimetres
    prep.depth = Image(fr->depth, depth_format, fr->render_width, fr->render_height, false, kRead, true);
    prep.motionVectors = Image(fr->motion, VK_FORMAT_R16G16_SFLOAT, fr->render_width, fr->render_height, false, kRead);
    if (!CameraFromViewProj(fr->view_proj, prep.cameraPosition, prep.cameraUp, prep.cameraRight, prep.cameraForward)) {
        // The host could not find the camera this frame: a neutral basis, and no interpolation across it.
        prep.cameraRight[0] = 1.0f; prep.cameraUp[1] = 1.0f; prep.cameraForward[2] = 1.0f;
        prep.reset = true;
    }
    r = g_api.dispatch(&g->ctx, &prep.header);
    if (r != FFX_API_RETURN_OK) {
        std::fprintf(stderr, "[fsr4-fg] prepare dispatch failed (%u)\n", r);
        return 1;
    }

    ffxDispatchDescFrameGeneration gen{};
    gen.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    gen.commandList = fr->cmd;
    gen.presentColor = Image(fr->cur_color, fr->color_format, fr->output_width, fr->output_height, false, kRead);
    gen.outputs[0] = Image(fr->output, VK_FORMAT_R16G16B16A16_SFLOAT, fr->output_width, fr->output_height, true, kWrite);
    gen.numGeneratedFrames = 1;
    gen.reset = prep.reset;
    gen.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB; // the host image is display-encoded (SDR)
    gen.minMaxLuminance[0] = 0.0f;
    gen.minMaxLuminance[1] = 80.0f;
    gen.generationRect = cfg.generationRect;
    gen.frameID = g->frame_id;
    r = g_api.dispatch(&g->ctx, &gen.header);
    if (r != FFX_API_RETURN_OK) {
        std::fprintf(stderr, "[fsr4-fg] generation dispatch failed (%u)\n", r);
        return 1;
    }
    // The runtime leaves its inputs readable; the host wants GENERAL.
    std::vector<VkImageMemoryBarrier> back;
    const auto add = [&](VkImage image, VkImageAspectFlags aspect) {
        VkImageMemoryBarrier m{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        m.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        m.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        m.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        m.image = image;
        m.subresourceRange = {aspect, 0, 1, 0, 1};
        back.push_back(m);
    };
    add(fr->cur_color, VK_IMAGE_ASPECT_COLOR_BIT);
    add(fr->motion, VK_IMAGE_ASPECT_COLOR_BIT);
    add(fr->depth, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT |
        ((depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)));
    g->barrier(fr->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
               static_cast<uint32_t>(back.size()), back.data());
    return 0;
}

void FgDestroy(void* instance) {
    auto* g = static_cast<Fg*>(instance);
    DestroyFg(g);
    delete g;
}

const KrFrameGenApi kFgApi = {KR_FRAMEGEN_ABI_VERSION, "FSR 4 frame generation", &FgCreate, &FgConfigure, &FgDispatch, &FgDestroy};

} // namespace

extern "C" {

KR_EXPORT const KrUpscalerApi* kr_upscaler_get_api(void) {
    return &kApi;
}

KR_EXPORT const KrFrameGenApi* kr_framegen_get_api(void) {
    return &kFgApi;
}

// Optional model choice (see UpscalerHost in the engine). Index 0 is "Auto"; the rest are the providers the AMD FSR runtime reports.
KR_EXPORT void kr_upscaler_set_model(int index) {
    g_model = index;
}

KR_EXPORT int kr_upscaler_models(void* instance, const char** names, int capacity) {
    auto* f = static_cast<Fsr4*>(instance);
    if (!f || capacity < 1) return 0;
    f->name_ptrs.assign(1, "Auto (chosen for this GPU)");
    for (const Version& v : g_versions) f->name_ptrs.push_back(v.name.c_str());
    const int n = static_cast<int>(f->name_ptrs.size()) < capacity ? static_cast<int>(f->name_ptrs.size()) : capacity;
    for (int i = 0; i < n; ++i) names[i] = f->name_ptrs[static_cast<size_t>(i)];
    return n;
}

KR_EXPORT const char* kr_upscaler_active_model(void*) {
    return g_active.c_str();
}

} // extern "C"
