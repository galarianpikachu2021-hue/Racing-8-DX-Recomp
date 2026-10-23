// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp upscaler plugin: NVIDIA DLSS (super resolution / DLAA) through NGX, as libkr_upscaler_dlss.so.
//
// The NGX SDK (NVIDIA/DLSS on GitHub) is fetched into a work folder by build.sh and linked statically; nothing of it is committed,
// and the resulting library must not be redistributed (the SDK's license). Only an NVIDIA GPU with a current driver can run it:
// `create` returns null elsewhere and the host falls back to another upscaler.
//
// Inputs (see kr_upscaler_abi.h): gamma-encoded LDR colour at the render size, scene depth, motion vectors in render pixels
// (prev = cur + mv, unjittered), the frame's jitter in render pixels. Output: the same format as the colour at the output size.
// NOT TESTED on hardware yet (the development machine has an AMD GPU): written from the NGX Vulkan programming guide and headers.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <libgen.h>

#include <vulkan/vulkan.h>

#include "nvsdk_ngx_defs.h"
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_vk.h"
#include "nvsdk_ngx_helpers_dlssg_vk.h"
#include "nvsdk_ngx_params.h"
#include "nvsdk_ngx_vk.h"

#include "kr_upscaler_abi.h"

namespace {

struct Dlss {
    KrUpscalerDevice dev{};
    NVSDK_NGX_Parameter* caps = nullptr;
    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    NVSDK_NGX_PerfQuality_Value quality = NVSDK_NGX_PerfQuality_Value_DLAA;
    int depth_inverted = 0;
    PFN_vkDeviceWaitIdle wait_idle = nullptr;
    PFN_vkCmdPipelineBarrier barrier = nullptr;
};

bool Ok(NVSDK_NGX_Result r, const char* what) {
    if (NVSDK_NGX_FAILED(r)) {
        std::fprintf(stderr, "[dlss] %s failed: 0x%08x\n", what, static_cast<unsigned>(r));
        return false;
    }
    return true;
}

/// Where this library lives: the NGX runtime snippets (libnvidia-ngx-dlss.so.*) are looked for next to it.
std::wstring LibraryDir() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&LibraryDir), &info) || !info.dli_fname) {
        return L".";
    }
    std::string path = info.dli_fname;
    std::vector<char> buf(path.begin(), path.end());
    buf.push_back('\0');
    const std::string dir = dirname(buf.data());
    return std::wstring(dir.begin(), dir.end());
}


// One NGX session per process, shared by the upscaler and the frame-generation backend of this library.
NVSDK_NGX_Parameter* g_caps = nullptr;
int g_ngx_refs = 0;

bool NgxAcquire(const KrUpscalerDevice* device, NVSDK_NGX_Parameter** caps) {
    if (g_ngx_refs == 0) {
        static const std::wstring dir = LibraryDir();
        static const wchar_t* paths[] = {dir.c_str()};
        NVSDK_NGX_FeatureCommonInfo info{};
        info.PathListInfo.Path = paths;
        info.PathListInfo.Length = 1;
        const std::wstring data_path = L"/tmp/kartrecomp-ngx";
        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Init_with_ProjectID("7c1f0f5e-6e3c-4a3b-9b5a-6d2a64f3a7d1", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0",
                                                               data_path.c_str(), device->instance, device->physical, device->device,
                                                               device->get_instance_proc_addr, device->get_device_proc_addr, &info,
                                                               NVSDK_NGX_Version_API);
        if (!Ok(r, "NVSDK_NGX_VULKAN_Init_with_ProjectID") || !Ok(NVSDK_NGX_VULKAN_GetCapabilityParameters(&g_caps), "GetCapabilityParameters")) {
            return false;
        }
    }
    ++g_ngx_refs;
    *caps = g_caps;
    return true;
}

void NgxRelease(VkDevice device) {
    if (g_ngx_refs > 0 && --g_ngx_refs == 0) {
        NVSDK_NGX_VULKAN_Shutdown1(device);
        g_caps = nullptr;
    }
}

NVSDK_NGX_PerfQuality_Value QualityFor(uint32_t render_w, uint32_t out_w) {
    const double ratio = out_w ? static_cast<double>(render_w) / out_w : 1.0;
    if (ratio >= 0.95) return NVSDK_NGX_PerfQuality_Value_DLAA;
    if (ratio >= 0.62) return NVSDK_NGX_PerfQuality_Value_MaxQuality;   // ~0.667
    if (ratio >= 0.54) return NVSDK_NGX_PerfQuality_Value_Balanced;     // ~0.58
    if (ratio >= 0.42) return NVSDK_NGX_PerfQuality_Value_MaxPerf;      // 0.5
    return NVSDK_NGX_PerfQuality_Value_UltraPerformance;                // ~0.33
}

void ReleaseFeature(Dlss* d) {
    if (d->feature) {
        if (d->wait_idle) d->wait_idle(d->dev.device);
        NVSDK_NGX_VULKAN_ReleaseFeature(d->feature);
        d->feature = nullptr;
    }
}

void* Create(const KrUpscalerDevice* device) {
    auto* d = new Dlss;
    d->dev = *device;
    d->wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(device->get_device_proc_addr(device->device, "vkDeviceWaitIdle"));
    d->barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->get_device_proc_addr(device->device, "vkCmdPipelineBarrier"));

    if (!NgxAcquire(device, &d->caps)) {
        delete d;
        return nullptr;
    }
    int available = 0;
    NVSDK_NGX_Parameter_GetI(d->caps, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!available) {
        int needs_driver = 0;
        NVSDK_NGX_Parameter_GetI(d->caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
        std::fprintf(stderr, "[dlss] DLSS is not available on this GPU/driver%s\n", needs_driver ? " (the driver needs an update)" : "");
        NgxRelease(device->device);
        delete d;
        return nullptr;
    }
    if (!Ok(NVSDK_NGX_VULKAN_AllocateParameters(&d->params), "AllocateParameters")) {
        NgxRelease(device->device);
        delete d;
        return nullptr;
    }
    return d;
}

int Configure(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat) {
    auto* d = static_cast<Dlss*>(instance);
    if (d->feature && d->rw == render_w && d->rh == render_h && d->ow == out_w && d->oh == out_h) {
        return 0;
    }
    ReleaseFeature(d); // the feature is created in Dispatch (NGX records its setup into the frame's command buffer)
    d->rw = render_w;
    d->rh = render_h;
    d->ow = out_w;
    d->oh = out_h;
    d->quality = QualityFor(render_w, out_w);
    return 0;
}

NVSDK_NGX_Resource_VK ImageRes(VkImage image, VkImageView view, VkFormat format, uint32_t w, uint32_t h, bool write,
                               VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    const VkImageSubresourceRange range{aspect, 0, 1, 0, 1};
    return NVSDK_NGX_Create_ImageView_Resource_VK(view, image, range, format, w, h, write);
}

void Transition(PFN_vkCmdPipelineBarrier barrier, VkCommandBuffer cmd, const std::vector<std::pair<VkImage, VkImageAspectFlags>>& images, VkImageLayout from,
                VkImageLayout to) {
    std::vector<VkImageMemoryBarrier> b;
    for (const auto& [image, aspect] : images) {
        VkImageMemoryBarrier m{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        m.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        m.oldLayout = from;
        m.newLayout = to;
        m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        m.image = image;
        m.subresourceRange = {aspect, 0, 1, 0, 1};
        b.push_back(m);
    }
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
               static_cast<uint32_t>(b.size()), b.data());
}

void Dispatch(void* instance, const KrUpscalerFrame* fr) {
    auto* d = static_cast<Dlss*>(instance);
    if (!d->rw || !fr->depth || !fr->motion) {
        return;
    }
    if (!d->feature) {
        NVSDK_NGX_DLSS_Create_Params create{};
        create.Feature.InWidth = d->rw;
        create.Feature.InHeight = d->rh;
        create.Feature.InTargetWidth = d->ow;
        create.Feature.InTargetHeight = d->oh;
        create.Feature.InPerfQualityValue = d->quality;
        // LDR colour; motion vectors at the render size and unjittered.
        d->depth_inverted = fr->depth_inverted ? 1 : 0;
        create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | (d->depth_inverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0);
        if (!Ok(NGX_VULKAN_CREATE_DLSS_EXT1(d->dev.device, fr->cmd, 1, 1, &d->feature, d->params, &create), "create DLSS feature")) {
            d->rw = 0; // do not retry every frame
            return;
        }
    }

    // NGX reads its inputs as sampled images: make that layout explicit, and give everything back in GENERAL afterwards.
    const VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    const std::vector<std::pair<VkImage, VkImageAspectFlags>> inputs = {
        {fr->color, VK_IMAGE_ASPECT_COLOR_BIT}, {fr->depth, depth_aspect}, {fr->motion, VK_IMAGE_ASPECT_COLOR_BIT}};
    Transition(d->barrier, fr->cmd, inputs, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    NVSDK_NGX_Resource_VK color = ImageRes(fr->color, fr->color_view, fr->color_format, fr->render_width, fr->render_height, false);
    NVSDK_NGX_Resource_VK depth = ImageRes(fr->depth, fr->depth_view, fr->depth_format, fr->render_width, fr->render_height, false, depth_aspect);
    NVSDK_NGX_Resource_VK motion = ImageRes(fr->motion, fr->motion_view, VK_FORMAT_R16G16_SFLOAT, fr->render_width, fr->render_height, false);
    NVSDK_NGX_Resource_VK output = ImageRes(fr->output, fr->output_view, fr->color_format, fr->output_width, fr->output_height, true);

    NVSDK_NGX_VK_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = &color;
    eval.Feature.pInOutput = &output;
    eval.Feature.InSharpness = 0.0f; // the host sharpens after the upscaler
    eval.pInDepth = &depth;
    eval.pInMotionVectors = &motion;
    eval.InJitterOffsetX = fr->jitter_x;
    eval.InJitterOffsetY = fr->jitter_y;
    eval.InRenderSubrectDimensions = {fr->render_width, fr->render_height};
    eval.InReset = fr->reset ? 1 : 0;
    eval.InMVScaleX = 1.0f;
    eval.InMVScaleY = 1.0f;
    eval.InPreExposure = fr->exposure > 0.0f ? fr->exposure : 1.0f;
    eval.InExposureScale = 1.0f;
    eval.InFrameTimeDeltaInMsec = fr->delta_time_ms > 0.0f ? fr->delta_time_ms : 16.6f;
    Ok(NGX_VULKAN_EVALUATE_DLSS_EXT(fr->cmd, d->feature, d->params, &eval), "evaluate DLSS");

    Transition(d->barrier, fr->cmd, inputs, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}

void Destroy(void* instance) {
    auto* d = static_cast<Dlss*>(instance);
    ReleaseFeature(d);
    if (d->params) NVSDK_NGX_VULKAN_DestroyParameters(d->params);
    NgxRelease(d->dev.device);
    delete d;
}

const KrUpscalerApi kApi = {KR_UPSCALER_ABI_VERSION, "dlss", &Create, &Configure, &Dispatch, &Destroy};

} // namespace

extern "C" __attribute__((visibility("default"))) const KrUpscalerApi* kr_upscaler_get_api(void) {
    return &kApi;
}

// ---------------------------------------------------------------------------------------------------------- frame generation
// DLSS 3 frame generation through NGX (RTX 40 and newer). NGX keeps the previous real frame itself, so every game frame is fed
// through `dispatch`, which asks for the frame halfway to the next one. NOT TESTED on hardware (AMD development machine).
namespace {

struct DlssG {
    KrUpscalerDevice dev{};
    NVSDK_NGX_Parameter* caps = nullptr;
    NVSDK_NGX_Parameter* params = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    PFN_vkDeviceWaitIdle wait_idle = nullptr;
    PFN_vkCmdPipelineBarrier barrier = nullptr;
};

// Row-major 4x4 inverse (cofactor expansion); returns false when singular.
bool Invert4(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

void Mul4(const float a[16], const float b[16], float out[16]) { // out = a * b
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = v;
        }
}

void* GCreate(const KrUpscalerDevice* device) {
    auto* d = new DlssG;
    d->dev = *device;
    d->wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(device->get_device_proc_addr(device->device, "vkDeviceWaitIdle"));
    d->barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->get_device_proc_addr(device->device, "vkCmdPipelineBarrier"));
    if (!NgxAcquire(device, &d->caps)) {
        delete d;
        return nullptr;
    }
    int available = 0;
    NVSDK_NGX_Parameter_GetI(d->caps, NVSDK_NGX_Parameter_FrameGeneration_Available, &available);
    if (!available) {
        std::fprintf(stderr, "[dlss] DLSS frame generation is not available on this GPU/driver\n");
        NgxRelease(device->device);
        delete d;
        return nullptr;
    }
    if (!Ok(NVSDK_NGX_VULKAN_AllocateParameters(&d->params), "AllocateParameters (DLSS-G)")) {
        NgxRelease(device->device);
        delete d;
        return nullptr;
    }
    return d;
}

int GConfigure(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat format) {
    auto* d = static_cast<DlssG*>(instance);
    if (d->feature && d->rw == render_w && d->rh == render_h && d->ow == out_w && d->oh == out_h && d->format == format) {
        return 0;
    }
    if (d->feature) {
        if (d->wait_idle) d->wait_idle(d->dev.device);
        NVSDK_NGX_VULKAN_ReleaseFeature(d->feature);
        d->feature = nullptr;
    }
    d->rw = render_w;
    d->rh = render_h;
    d->ow = out_w;
    d->oh = out_h;
    d->format = format;
    return 0; // the feature is created in dispatch, on the frame's command buffer
}

int GDispatch(void* instance, const KrFrameGenFrame* fr) {
    auto* d = static_cast<DlssG*>(instance);
    if (!d->rw || !fr->motion || !fr->depth) {
        return 1;
    }
    if (!d->feature) {
        NVSDK_NGX_DLSSG_Create_Params create{};
        create.Width = d->ow;
        create.Height = d->oh;
        create.NativeBackbufferFormat = static_cast<unsigned>(d->format);
        create.RenderWidth = d->rw;
        create.RenderHeight = d->rh;
        create.DynamicResolutionScaling = false;
        if (!Ok(NGX_VK_CREATE_DLSSG(fr->cmd, 1, 1, &d->feature, d->params, &create), "create DLSS-G feature")) {
            d->rw = 0; // do not retry every frame
            return 1;
        }
    }

    const std::vector<std::pair<VkImage, VkImageAspectFlags>> inputs = {
        {fr->cur_color, VK_IMAGE_ASPECT_COLOR_BIT}, {fr->depth, VK_IMAGE_ASPECT_DEPTH_BIT}, {fr->motion, VK_IMAGE_ASPECT_COLOR_BIT}};
    Transition(d->barrier, fr->cmd, inputs, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    NVSDK_NGX_Resource_VK backbuffer = ImageRes(fr->cur_color, fr->cur_color_view, fr->color_format, fr->output_width, fr->output_height, false);
    NVSDK_NGX_Resource_VK depth = ImageRes(fr->depth, fr->depth_view, fr->depth_format, fr->render_width, fr->render_height, false, VK_IMAGE_ASPECT_DEPTH_BIT);
    NVSDK_NGX_Resource_VK motion = ImageRes(fr->motion, fr->motion_view, VK_FORMAT_R16G16_SFLOAT, fr->render_width, fr->render_height, false);
    NVSDK_NGX_Resource_VK out = ImageRes(fr->output, fr->output_view, VK_FORMAT_R16G16B16A16_SFLOAT, fr->output_width, fr->output_height, true);

    NVSDK_NGX_VK_DLSSG_Eval_Params eval{};
    eval.pBackbuffer = &backbuffer;
    eval.pDepth = &depth;
    eval.pMVecs = &motion;
    eval.pOutputInterpFrame = &out;

    NVSDK_NGX_DLSSG_Opt_Eval_Params opt{};
    opt.multiFrameCount = 1;
    opt.multiFrameIndex = 1;
    float inv_vp[16], clip_to_prev[16], prev_to_clip[16], inv_prev[16];
    if (Invert4(fr->view_proj, inv_vp) && Invert4(fr->prev_view_proj, inv_prev)) {
        Mul4(fr->prev_view_proj, inv_vp, clip_to_prev);
        Mul4(fr->view_proj, inv_prev, prev_to_clip);
        std::memcpy(opt.clipToPrevClip, clip_to_prev, sizeof(clip_to_prev));
        std::memcpy(opt.prevClipToClip, prev_to_clip, sizeof(prev_to_clip));
    }
    opt.jitterOffset[0] = 0.0f; // jitter is already out of the colour and the motion vectors
    opt.jitterOffset[1] = 0.0f;
    opt.mvecScale[0] = d->rw ? 1.0f / d->rw : 1.0f; // render pixels -> [-1,1]-ish
    opt.mvecScale[1] = d->rh ? 1.0f / d->rh : 1.0f;
    opt.cameraNear = fr->camera_near;
    opt.cameraFar = fr->camera_far;
    opt.cameraFOV = fr->camera_fov_y;
    opt.cameraAspectRatio = d->oh ? static_cast<float>(d->ow) / d->oh : 16.0f / 9.0f;
    opt.colorBuffersHDR = d->format == VK_FORMAT_R16G16B16A16_SFLOAT || d->format == VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    opt.depthInverted = fr->depth_inverted != 0;
    opt.cameraMotionIncluded = true;
    opt.reset = fr->reset != 0;
    opt.motionVectorsInvalidValue = 0.0f;
    opt.motionVectorsDilated = false;
    opt.menuDetectionEnabled = false;
    const bool ok = Ok(NGX_VK_EVALUATE_DLSSG(fr->cmd, d->feature, d->params, &eval, &opt), "evaluate DLSS-G");

    Transition(d->barrier, fr->cmd, inputs, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    return ok ? 0 : 1;
}

void GDestroy(void* instance) {
    auto* d = static_cast<DlssG*>(instance);
    GConfigure(d, 0, 0, 0, 0, VK_FORMAT_UNDEFINED);
    if (d->params) NVSDK_NGX_VULKAN_DestroyParameters(d->params);
    NgxRelease(d->dev.device);
    delete d;
}

const KrFrameGenApi kGApi = {KR_FRAMEGEN_ABI_VERSION, "dlss-g", &GCreate, &GConfigure, &GDispatch, &GDestroy};

} // namespace

extern "C" __attribute__((visibility("default"))) const KrFrameGenApi* kr_framegen_get_api(void) {
    return &kGApi;
}
