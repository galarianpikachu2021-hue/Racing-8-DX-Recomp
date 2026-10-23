// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp: AMD FidelityFX Super Resolution 3.1 upscaler as a plugin for the native renderer's upscaler hook
// (libkr_upscaler_fsr3.so, loaded with KARTRECOMP_UPSCALER=fsr3). This file is our wrapper only: the FidelityFX SDK
// (MIT license) is fetched and built by build.sh and linked in; it is never committed to this repository.
//
// The host hands over tonemapped colour at render size, scene depth (non-inverted, infinite far plane), camera-motion
// vectors in render pixels (unjittered, prev = cur + mv) and this frame's jitter. FSR writes an RGBA16F image (its shaders
// declare that format), which is then converted into the host's output image with a blit.
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/ffx_opticalflow.h>

#include "colorspace_spv.h"
#include "kr_upscaler_abi.h"

namespace {

PFN_vkGetDeviceProcAddr g_real_gdpa = nullptr;

// Experiment switches (KARTRECOMP_FSR3_OPT=lin,jflipx,...): for comparing against other upscalers on dumped frames. `lin` converts
// the colour from sRGB to linear around FSR (AMD's own FSR 3.1.5 doesn't convert, and the output matches it only without).
// KARTRECOMP_FSR3_PARAMS=name=value,...: override a dispatch parameter (vs2m, near, fov, dt, preexp).
float Param(const char* name, float fallback) {
    static const std::string params = std::getenv("KARTRECOMP_FSR3_PARAMS") ? std::string(",") + std::getenv("KARTRECOMP_FSR3_PARAMS") + "," : std::string();
    const size_t at = params.find(std::string(",") + name + "=");
    return at == std::string::npos ? fallback : static_cast<float>(std::atof(params.c_str() + at + std::strlen(name) + 2));
}

bool Opt(const char* name) {
    static const std::string opts = std::getenv("KARTRECOMP_FSR3_OPT") ? std::string(",") + std::getenv("KARTRECOMP_FSR3_OPT") + "," : std::string();
    return opts.find(std::string(",") + name + ",") != std::string::npos;
}

// Debug-utils and buffer-marker entry points the backend calls without checking: no-ops when the device lacks them.
VKAPI_ATTR VkResult VKAPI_CALL NoopSetObjectName(VkDevice, const VkDebugUtilsObjectNameInfoEXT*) { return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL NoopCmdLabel(VkCommandBuffer, const VkDebugUtilsLabelEXT*) {}
VKAPI_ATTR void VKAPI_CALL NoopCmdEndLabel(VkCommandBuffer) {}
VKAPI_ATTR void VKAPI_CALL NoopCmdMarker(VkCommandBuffer, VkPipelineStageFlagBits, VkBuffer, VkDeviceSize, uint32_t) {}
VKAPI_ATTR void VKAPI_CALL NoopCmdMarker2(VkCommandBuffer, VkPipelineStageFlags2, VkBuffer, VkDeviceSize, uint32_t) {}

// The backend asks for some core functions by their extension names (vkGetBufferMemoryRequirements2KHR) that this device
// only exposes under the core name, since the extension itself isn't enabled.
PFN_vkVoidFunction WrapGetDeviceProcAddr(VkDevice device, const char* name) {
    PFN_vkVoidFunction fn = g_real_gdpa(device, name);
    if (!fn) {
        std::string core(name);
        for (const char* suffix : {"KHR", "EXT"}) {
            const size_t n = std::strlen(suffix);
            if (core.size() > n && core.compare(core.size() - n, n, suffix) == 0) {
                core.resize(core.size() - n);
                fn = g_real_gdpa(device, core.c_str());
                break;
            }
        }
    }
    if (!fn) {
        const std::string n(name);
        if (n == "vkSetDebugUtilsObjectNameEXT") fn = reinterpret_cast<PFN_vkVoidFunction>(&NoopSetObjectName);
        else if (n == "vkCmdBeginDebugUtilsLabelEXT") fn = reinterpret_cast<PFN_vkVoidFunction>(&NoopCmdLabel);
        else if (n == "vkCmdEndDebugUtilsLabelEXT") fn = reinterpret_cast<PFN_vkVoidFunction>(&NoopCmdEndLabel);
        else if (n == "vkCmdWriteBufferMarkerAMD") fn = reinterpret_cast<PFN_vkVoidFunction>(&NoopCmdMarker);
        else if (n == "vkCmdWriteBufferMarker2AMD") fn = reinterpret_cast<PFN_vkVoidFunction>(&NoopCmdMarker2);
    }
    return fn;
}

struct Img {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t w = 0, h = 0;
    VkImageUsageFlags usage = 0;
};

struct Fsr3;
Fsr3* g_up = nullptr; ///< the configured upscaler instance: the frame-generation backend reads its shared images

struct Fsr3 {
    KrUpscalerDevice dev{};
    PFN_vkGetPhysicalDeviceMemoryProperties get_mem_props = nullptr;
    PFN_vkCreateImage create_image = nullptr;
    PFN_vkDestroyImage destroy_image = nullptr;
    PFN_vkGetImageMemoryRequirements get_reqs = nullptr;
    PFN_vkAllocateMemory alloc_memory = nullptr;
    PFN_vkFreeMemory free_memory = nullptr;
    PFN_vkBindImageMemory bind_memory = nullptr;
    PFN_vkCmdPipelineBarrier cmd_barrier = nullptr;
    PFN_vkCreateShaderModule create_shader = nullptr;
    PFN_vkCreateDescriptorSetLayout create_dsl = nullptr;
    PFN_vkCreateDescriptorPool create_pool = nullptr;
    PFN_vkAllocateDescriptorSets alloc_sets = nullptr;
    PFN_vkUpdateDescriptorSets update_sets = nullptr;
    PFN_vkCreatePipelineLayout create_layout = nullptr;
    PFN_vkCreateComputePipelines create_pipelines = nullptr;
    PFN_vkCmdBindPipeline cmd_bind_pipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmd_bind_sets = nullptr;
    PFN_vkCmdPushConstants cmd_push = nullptr;
    PFN_vkCmdDispatch cmd_dispatch = nullptr;
    PFN_vkCreateImageView create_view = nullptr;
    PFN_vkDestroyImageView destroy_view = nullptr;
    PFN_vkCreateSampler create_sampler = nullptr;
    PFN_vkDeviceWaitIdle wait_idle = nullptr;

    VkDeviceContext devctx{};
    void* scratch = nullptr;
    FfxFsr3UpscalerContext ctx{};
    FfxFsr3UpscalerContextDescription desc{};
    bool have_ctx = false;
    uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    VkFormat out_format = VK_FORMAT_UNDEFINED;

    // Colour space: the host's colour is sRGB-encoded, FSR 3.1 wants linear. to_linear: host colour -> lin_in; from_linear: out16 -> host output.
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set_in = VK_NULL_HANDLE, set_out = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline to_linear = VK_NULL_HANDLE, from_linear = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Img lin_in;                     ///< linear RGBA16F colour at render size
    VkImageView lin_in_view = VK_NULL_HANDLE, out16_view = VK_NULL_HANDLE;
    Img out16;                      ///< RGBA16F (linear), what FSR writes
    Img reactive;                   ///< R8 reactive mask at render size, from the opaque/final colour pair
    Img reactive_final;             ///< the larger of that and the host's mismatch mask: what FSR gets
    VkDescriptorSetLayout dsl3 = VK_NULL_HANDLE;
    VkPipelineLayout layout3 = VK_NULL_HANDLE;
    VkPipeline combine = VK_NULL_HANDLE;
    VkDescriptorSet set_combine = VK_NULL_HANDLE;
    VkImageView reactive_view = VK_NULL_HANDLE, reactive_final_view = VK_NULL_HANDLE;
    Img dilated_depth, dilated_mv, prev_nearest_depth; ///< the shared resources FSR 3 wants the application to own
    bool fresh = true;              ///< images not yet moved to GENERAL
};

void Message(FfxMsgType type, const wchar_t* message) {
    if (type == FFX_MESSAGE_TYPE_ERROR) {
        std::fprintf(stderr, "[fsr3] error: %ls\n", message);
    } else if (std::getenv("KARTRECOMP_FSR3_VERBOSE")) {
        std::fprintf(stderr, "[fsr3] %ls\n", message);
    }
}

void Teardown(Fsr3* f) {
    if (f->have_ctx) {
        if (f->wait_idle) f->wait_idle(f->dev.device);
        ffxFsr3UpscalerContextDestroy(&f->ctx);
        f->have_ctx = false;
    }
    if (f->reactive_view) f->destroy_view(f->dev.device, f->reactive_view, nullptr);
    if (f->reactive_final_view) f->destroy_view(f->dev.device, f->reactive_final_view, nullptr);
    f->reactive_view = f->reactive_final_view = VK_NULL_HANDLE;
    if (f->lin_in_view) f->destroy_view(f->dev.device, f->lin_in_view, nullptr);
    if (f->out16_view) f->destroy_view(f->dev.device, f->out16_view, nullptr);
    f->lin_in_view = f->out16_view = VK_NULL_HANDLE;
    for (Img* i : {&f->reactive_final, &f->reactive, &f->lin_in, &f->out16, &f->dilated_depth, &f->dilated_mv, &f->prev_nearest_depth}) {
        if (i->image) f->destroy_image(f->dev.device, i->image, nullptr);
        if (i->memory) f->free_memory(f->dev.device, i->memory, nullptr);
        *i = {};
    }
}

void* Create(const KrUpscalerDevice* device) {
    auto* f = new Fsr3();
    f->dev = *device;
    const auto gipa = device->get_instance_proc_addr;
    const auto gdpa = device->get_device_proc_addr;
    f->get_mem_props = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        gipa(device->instance, "vkGetPhysicalDeviceMemoryProperties"));
#define LOAD(member, name) f->member = reinterpret_cast<PFN_##name>(gdpa(device->device, #name))
    LOAD(create_image, vkCreateImage);
    LOAD(destroy_image, vkDestroyImage);
    LOAD(get_reqs, vkGetImageMemoryRequirements);
    LOAD(alloc_memory, vkAllocateMemory);
    LOAD(free_memory, vkFreeMemory);
    LOAD(bind_memory, vkBindImageMemory);
    LOAD(cmd_barrier, vkCmdPipelineBarrier);
    LOAD(create_shader, vkCreateShaderModule);
    LOAD(create_dsl, vkCreateDescriptorSetLayout);
    LOAD(create_pool, vkCreateDescriptorPool);
    LOAD(alloc_sets, vkAllocateDescriptorSets);
    LOAD(update_sets, vkUpdateDescriptorSets);
    LOAD(create_layout, vkCreatePipelineLayout);
    LOAD(create_pipelines, vkCreateComputePipelines);
    LOAD(cmd_bind_pipeline, vkCmdBindPipeline);
    LOAD(cmd_bind_sets, vkCmdBindDescriptorSets);
    LOAD(cmd_push, vkCmdPushConstants);
    LOAD(cmd_dispatch, vkCmdDispatch);
    LOAD(create_view, vkCreateImageView);
    LOAD(destroy_view, vkDestroyImageView);
    LOAD(create_sampler, vkCreateSampler);
    LOAD(wait_idle, vkDeviceWaitIdle);
#undef LOAD
    g_real_gdpa = device->get_device_proc_addr;
    f->devctx = {device->device, device->physical, &WrapGetDeviceProcAddr};
    return f;
}

uint32_t rw_(const Fsr3* f) { return f->rw; }
uint32_t rh_(const Fsr3* f) { return f->rh; }

bool MakeImage(Fsr3* f, Img& img, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (f->create_image(f->dev.device, &ici, nullptr, &img.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    f->get_reqs(f->dev.device, img.image, &req);
    VkPhysicalDeviceMemoryProperties mp;
    f->get_mem_props(f->dev.physical, &mp);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = i;
            break;
        }
    }
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (f->alloc_memory(f->dev.device, &mai, nullptr, &img.memory) != VK_SUCCESS) return false;
    f->bind_memory(f->dev.device, img.image, img.memory, 0);
    img.format = format;
    img.w = w;
    img.h = h;
    img.usage = usage;
    f->fresh = true;
    return true;
}

VkFormat VkFormatOf(FfxSurfaceFormat format) {
    switch (format) {
    case FFX_SURFACE_FORMAT_R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
    case FFX_SURFACE_FORMAT_R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
    case FFX_SURFACE_FORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
    case FFX_SURFACE_FORMAT_R16G16_SINT: return VK_FORMAT_R16G16_SINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

VkImageView MakeView(Fsr3* f, const Img& img) {
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = img.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView v = VK_NULL_HANDLE;
    f->create_view(f->dev.device, &vci, nullptr, &v);
    return v;
}

// Pipelines, descriptor sets and the views over our own images (pipelines are created once; views follow the images).
bool MakeColorspace(Fsr3* f) {
    f->lin_in_view = MakeView(f, f->lin_in);
    f->out16_view = MakeView(f, f->out16);
    f->reactive_view = MakeView(f, f->reactive);
    f->reactive_final_view = MakeView(f, f->reactive_final);
    if (f->to_linear) return f->lin_in_view && f->out16_view;
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    f->create_sampler(f->dev.device, &sci, nullptr, &f->sampler);
    const VkDescriptorSetLayoutBinding b[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                               {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dlci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dlci.bindingCount = 2;
    dlci.pBindings = b;
    f->create_dsl(f->dev.device, &dlci, nullptr, &f->dsl);
    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 2;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = sizes;
    f->create_pool(f->dev.device, &dpci, nullptr, &f->pool);
    const VkDescriptorSetLayout layouts[2] = {f->dsl, f->dsl};
    VkDescriptorSet sets[2]{};
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = f->pool;
    dsai.descriptorSetCount = 2;
    dsai.pSetLayouts = layouts;
    f->alloc_sets(f->dev.device, &dsai, sets);
    f->set_in = sets[0];
    f->set_out = sets[1];
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, 12};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &f->dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push;
    f->create_layout(f->dev.device, &plci, nullptr, &f->layout);
    const auto make = [&](const uint32_t* code, size_t bytes, VkPipeline& out) {
        VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mci.codeSize = bytes;
        mci.pCode = code;
        VkShaderModule mod = VK_NULL_HANDLE;
        if (f->create_shader(f->dev.device, &mci, nullptr, &mod) != VK_SUCCESS) return false;
        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr};
        cpci.layout = f->layout;
        return f->create_pipelines(f->dev.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &out) == VK_SUCCESS;
    };
    if (!make(kToLinearSpv, sizeof(kToLinearSpv), f->to_linear) || !make(kFromLinearSpv, sizeof(kFromLinearSpv), f->from_linear)) {
        std::fprintf(stderr, "[fsr3] cannot create the colour-space pipelines\n");
        return false;
    }
    // The mask combiner: two sampled masks in, one storage mask out.
    {
        const VkDescriptorSetLayoutBinding b3[3] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                    {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
                                                    {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo d3{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        d3.bindingCount = 3;
        d3.pBindings = b3;
        f->create_dsl(f->dev.device, &d3, nullptr, &f->dsl3);
        const VkDescriptorPoolSize sz[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
        VkDescriptorPoolCreateInfo p3{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        p3.maxSets = 1;
        p3.poolSizeCount = 2;
        p3.pPoolSizes = sz;
        VkDescriptorPool pool3 = VK_NULL_HANDLE;
        f->create_pool(f->dev.device, &p3, nullptr, &pool3);
        VkDescriptorSetAllocateInfo a3{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        a3.descriptorPool = pool3;
        a3.descriptorSetCount = 1;
        a3.pSetLayouts = &f->dsl3;
        f->alloc_sets(f->dev.device, &a3, &f->set_combine);
        const VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
        VkPipelineLayoutCreateInfo pl3{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl3.setLayoutCount = 1;
        pl3.pSetLayouts = &f->dsl3;
        pl3.pushConstantRangeCount = 1;
        pl3.pPushConstantRanges = &pc;
        f->create_layout(f->dev.device, &pl3, nullptr, &f->layout3);
        VkShaderModuleCreateInfo mci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mci.codeSize = sizeof(kCombineSpv);
        mci.pCode = kCombineSpv;
        VkShaderModule mod = VK_NULL_HANDLE;
        if (f->create_shader(f->dev.device, &mci, nullptr, &mod) != VK_SUCCESS) return false;
        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main", nullptr};
        cpci.layout = f->layout3;
        if (f->create_pipelines(f->dev.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &f->combine) != VK_SUCCESS) return false;
    }
    return f->lin_in_view && f->out16_view;
}

// FSR 3.1's temporal tuning. The SDK defaults are velocity 1, reactiveness 1, shading change 1, accumulation 0.333, minimum
// disocclusion accumulation -0.333; see docs/UPSCALER.md for how the values here were chosen.
constexpr float kTuneVelocity = 1.0f;
constexpr float kTuneReactiveness = 1.0f;
constexpr float kTuneShadingChange = 2.0f; // SDK default 1: reacts faster to shading changes, a little less trailing
constexpr float kTuneAccumulation = 0.333f;
constexpr float kTuneMinDisocclusion = -0.333f;

int Configure(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat color_format) {
    auto* f = static_cast<Fsr3*>(instance);
    if (f->have_ctx && f->rw == render_w && f->rh == render_h && f->ow == out_w && f->oh == out_h &&
        f->out_format == color_format) {
        return 0;
    }
    Teardown(f);
    f->rw = render_w;
    f->rh = render_h;
    f->ow = out_w;
    f->oh = out_h;
    f->out_format = color_format;
    if (!MakeImage(f, f->out16, VK_FORMAT_R16G16B16A16_SFLOAT, f->ow, f->oh,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        std::fprintf(stderr, "[fsr3] cannot allocate the output image\n");
        return 1;
    }
    if (!MakeImage(f, f->lin_in, VK_FORMAT_R16G16B16A16_SFLOAT, rw_(f), rh_(f), VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        std::fprintf(stderr, "[fsr3] cannot allocate the linear colour image\n");
        return 1;
    }
    if (!MakeImage(f, f->reactive, VK_FORMAT_R8_UNORM, rw_(f), rh_(f), VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        std::fprintf(stderr, "[fsr3] cannot allocate the reactive mask\n");
        return 1;
    }
    if (!MakeImage(f, f->reactive_final, VK_FORMAT_R8_UNORM, rw_(f), rh_(f), VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        std::fprintf(stderr, "[fsr3] cannot allocate the final reactive mask\n");
        return 1;
    }
    if (!MakeColorspace(f)) return 1;
    if (!f->scratch) {
        const size_t size = ffxGetScratchMemorySizeVK(f->dev.physical, FFX_FSR3UPSCALER_CONTEXT_COUNT);
        // The backend's context types are alignas(32): plain calloc (16) is not enough.
        const size_t rounded = (size + 63) & ~size_t{63};
        f->scratch = std::aligned_alloc(64, rounded);
        if (f->scratch) std::memset(f->scratch, 0, rounded);
        FfxErrorCode e = ffxGetInterfaceVK(&f->desc.backendInterface, ffxGetDeviceVK(&f->devctx), f->scratch, size,
                                           FFX_FSR3UPSCALER_CONTEXT_COUNT);
        if (e != FFX_OK) {
            std::fprintf(stderr, "[fsr3] ffxGetInterfaceVK failed (%d)\n", static_cast<int>(e));
            return 1;
        }
    }
    f->desc.flags = FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE; // non-inverted depth, infinite far plane, LDR colour, low-res MVs
    if (Opt("noinf")) f->desc.flags = 0;
    if (Opt("hdr")) f->desc.flags |= FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE;
    if (Opt("autoexp")) f->desc.flags |= FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
    if (Opt("jcancel")) f->desc.flags |= FFX_FSR3UPSCALER_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if (std::getenv("KARTRECOMP_FSR3_DEBUG")) f->desc.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
    f->desc.maxRenderSize = {render_w, render_h};
    f->desc.maxUpscaleSize = {out_w, out_h};
    f->desc.fpMessage = &Message;
    const FfxErrorCode e = ffxFsr3UpscalerContextCreate(&f->ctx, &f->desc);
    if (e != FFX_OK) {
        std::fprintf(stderr, "[fsr3] ffxFsr3UpscalerContextCreate failed (%d)\n", static_cast<int>(e));
        return 1;
    }
    f->have_ctx = true;
    // Temporal tuning (AMD's keys): defaults are the SDK's; KARTRECOMP_FSR3_PARAMS overrides for experiments (velocity, reactscale,
    // shadescale, accumadd, mindisocc).
    {
        const struct { FfxFsr3UpscalerConfigureKey key; const char* name; float value; } tune[] = {
            {FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FVELOCITYFACTOR, "velocity", kTuneVelocity},
            {FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FREACTIVENESSSCALE, "reactscale", kTuneReactiveness},
            {FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FSHADINGCHANGESCALE, "shadescale", kTuneShadingChange},
            {FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FACCUMULATIONADDEDPERFRAME, "accumadd", kTuneAccumulation},
            {FFX_FSR3UPSCALER_CONFIGURE_UPSCALE_KEY_FMINDISOCCLUSIONACCUMULATION, "mindisocc", kTuneMinDisocclusion}};
        for (const auto& t : tune) {
            float v = Param(t.name, t.value);
            ffxFsr3UpscalerSetConstant(&f->ctx, t.key, &v);
        }
    }
    FfxFsr3UpscalerSharedResourceDescriptions shared{};
    if (ffxFsr3UpscalerGetSharedResourceDescriptions(&f->ctx, &shared) != FFX_OK) {
        std::fprintf(stderr, "[fsr3] cannot query the shared resources\n");
        return 1;
    }
    const VkImageUsageFlags shared_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    struct { Img* img; const FfxCreateResourceDescription* d; } list[] = {{&f->dilated_depth, &shared.dilatedDepth},
                                                                      {&f->dilated_mv, &shared.dilatedMotionVectors},
                                                                      {&f->prev_nearest_depth, &shared.reconstructedPrevNearestDepth}};
    for (auto& e : list) {
        const VkFormat fmt = VkFormatOf(e.d->resourceDescription.format);
        if (fmt == VK_FORMAT_UNDEFINED || !MakeImage(f, *e.img, fmt, e.d->resourceDescription.width, e.d->resourceDescription.height, shared_usage)) {
            std::fprintf(stderr, "[fsr3] cannot allocate a shared resource\n");
            return 1;
        }
    }
    std::fprintf(stderr, "[fsr3] FSR 3.1 context: %ux%u -> %ux%u\n", render_w, render_h, out_w, out_h);
    g_up = f;
    return 0;
}

FfxResource Wrap(VkImage image, VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage, FfxResourceUsage extra,
                 const wchar_t* name) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.usage = usage;
    // All of the host's images sit in GENERAL: say so, the backend transitions what it needs.
    return ffxGetResourceVK(image, ffxGetImageResourceDescriptionVK(image, ici, extra), name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
}

void Dispatch(void* instance, const KrUpscalerFrame* fr) {
    auto* f = static_cast<Fsr3*>(instance);
    if (!f->have_ctx || !fr->depth || !fr->motion) return;
    if (f->fresh) {
        // First use: move our own images from UNDEFINED to GENERAL, the layout everything here is declared in.
        std::vector<VkImageMemoryBarrier> init;
        for (const Img* i : {&f->reactive_final, &f->reactive, &f->lin_in, &f->out16, &f->dilated_depth, &f->dilated_mv, &f->prev_nearest_depth}) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = i->image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            init.push_back(b);
        }
        f->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                       static_cast<uint32_t>(init.size()), init.data());
        f->fresh = false;
    }
    const VkFormat depth_format = fr->depth_format != VK_FORMAT_UNDEFINED ? fr->depth_format : VK_FORMAT_D32_SFLOAT;

    // Host colour (sRGB-encoded) -> linear RGBA16F, which is what FSR 3.1 expects for LDR input.
    const auto run = [&](VkPipeline pipe, VkDescriptorSet set, VkImageView src, VkImageView dst, uint32_t w, uint32_t h) {
        const VkDescriptorImageInfo si{f->sampler, src, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo di{VK_NULL_HANDLE, dst, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet wr[2]{};
        wr[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &si};
        wr[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &di};
        f->update_sets(f->dev.device, 2, wr, 0, nullptr);
        f->cmd_bind_pipeline(fr->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        f->cmd_bind_sets(fr->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, f->layout, 0, 1, &set, 0, nullptr);
        const int32_t size[3] = {static_cast<int32_t>(w), static_cast<int32_t>(h), Opt("lin") ? 0 : 1};
        f->cmd_push(fr->cmd, f->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, size);
        f->cmd_dispatch(fr->cmd, (w + 7) / 8, (h + 7) / 8, 1);
    };
    const auto compute_barrier = [&]() {
        VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        m.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        f->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &m, 0, nullptr, 0, nullptr);
    };
    compute_barrier();
    run(f->to_linear, f->set_in, fr->color_view, f->lin_in_view, fr->render_width, fr->render_height);
    compute_barrier();

    // Reactive mask: where alpha-blended effects were drawn (AMD's guidance for particles and glass, which have no motion vectors).
    const bool have_reactive = fr->opaque_color && fr->scene_color && !Opt("noreactive");
    const bool have_mismatch = fr->mismatch_mask_view && !Opt("nomismatch");
    FfxResource reactive_res{};
    if (have_reactive) {
        FfxFsr3UpscalerGenerateReactiveDescription g{};
        g.commandList = ffxGetCommandListVK(fr->cmd);
        const VkFormat sf = fr->scene_color_format != VK_FORMAT_UNDEFINED ? fr->scene_color_format : VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        g.colorOpaqueOnly = Wrap(fr->opaque_color, sf, fr->render_width, fr->render_height, VK_IMAGE_USAGE_SAMPLED_BIT,
                                 FFX_RESOURCE_USAGE_READ_ONLY, L"KR_FSR3_OpaqueColor");
        g.colorPreUpscale = Wrap(fr->scene_color, sf, fr->render_width, fr->render_height, VK_IMAGE_USAGE_SAMPLED_BIT,
                                 FFX_RESOURCE_USAGE_READ_ONLY, L"KR_FSR3_SceneColor");
        reactive_res = Wrap(f->reactive.image, f->reactive.format, fr->render_width, fr->render_height, f->reactive.usage,
                            FFX_RESOURCE_USAGE_UAV, L"KR_FSR3_Reactive");
        g.outReactive = reactive_res;
        g.renderSize = {fr->render_width, fr->render_height};
        g.scale = Param("rscale", 1.0f);
        g.cutoffThreshold = Param("rcutoff", 0.2f);
        g.binaryValue = Param("rvalue", 0.9f);
        // SDK flag values (gpu/fsr2/ffx_fsr2_resources.h): 1 = apply tonemap (the scene colour is HDR), 4 = threshold, 8 = max component.
        g.flags = 1 | 4 | 8;
        if (ffxFsr3UpscalerContextGenerateReactiveMask(&f->ctx, &g) != FFX_OK) {
            std::fprintf(stderr, "[fsr3] reactive mask generation failed\n");
            reactive_res = {};
        }
    }

    if (have_mismatch || have_reactive) {
        // Final reactive mask: the larger of FSR's alpha-blend mask and the host's mismatch mask.
        const VkDescriptorImageInfo ia{f->sampler, f->reactive_view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo ib{f->sampler, have_mismatch ? fr->mismatch_mask_view : f->reactive_view, VK_IMAGE_LAYOUT_GENERAL};
        const VkDescriptorImageInfo id{VK_NULL_HANDLE, f->reactive_final_view, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet wr[3]{};
        wr[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f->set_combine, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ia};
        wr[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f->set_combine, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ib};
        wr[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, f->set_combine, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &id};
        f->update_sets(f->dev.device, 3, wr, 0, nullptr);
        // FSR's own mask pass left `reactive` read-only: back to GENERAL before we sample it.
        if (have_reactive) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = f->reactive.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            f->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        }
        compute_barrier();
        f->cmd_bind_pipeline(fr->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, f->combine);
        f->cmd_bind_sets(fr->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, f->layout3, 0, 1, &f->set_combine, 0, nullptr);
        const int32_t pc[4] = {static_cast<int32_t>(fr->render_width), static_cast<int32_t>(fr->render_height), have_reactive ? 1 : 0,
                               have_mismatch ? 1 : 0};
        f->cmd_push(fr->cmd, f->layout3, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
        f->cmd_dispatch(fr->cmd, (fr->render_width + 7) / 8, (fr->render_height + 7) / 8, 1);
        compute_barrier();
        reactive_res = Wrap(f->reactive_final.image, f->reactive_final.format, fr->render_width, fr->render_height, f->reactive_final.usage,
                            FFX_RESOURCE_USAGE_UAV, L"KR_FSR3_ReactiveFinal");
    }

    FfxFsr3UpscalerDispatchDescription d{};
    d.reactive = reactive_res;
    d.commandList = ffxGetCommandListVK(fr->cmd);
    d.color = Wrap(f->lin_in.image, f->lin_in.format, fr->render_width, fr->render_height, f->lin_in.usage, FFX_RESOURCE_USAGE_READ_ONLY,
                   L"KR_FSR3_Color");
    d.depth = Wrap(fr->depth, depth_format, fr->render_width, fr->render_height,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, FFX_RESOURCE_USAGE_READ_ONLY,
                   L"KR_FSR3_Depth");
    d.motionVectors = Wrap(fr->motion, VK_FORMAT_R16G16_SFLOAT, fr->render_width, fr->render_height, VK_IMAGE_USAGE_SAMPLED_BIT,
                           FFX_RESOURCE_USAGE_READ_ONLY, L"KR_FSR3_Motion");
    d.output = Wrap(f->out16.image, f->out16.format, f->ow, f->oh, f->out16.usage, FFX_RESOURCE_USAGE_UAV, L"KR_FSR3_Output");
    const FfxResourceUsage shared_rt_uav = static_cast<FfxResourceUsage>(FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV);
    d.dilatedDepth = Wrap(f->dilated_depth.image, f->dilated_depth.format, f->dilated_depth.w, f->dilated_depth.h,
                          f->dilated_depth.usage, shared_rt_uav, L"KR_FSR3_DilatedDepth");
    d.dilatedMotionVectors = Wrap(f->dilated_mv.image, f->dilated_mv.format, f->dilated_mv.w, f->dilated_mv.h, f->dilated_mv.usage,
                                  shared_rt_uav, L"KR_FSR3_DilatedMotion");
    d.reconstructedPrevNearestDepth = Wrap(f->prev_nearest_depth.image, f->prev_nearest_depth.format, f->prev_nearest_depth.w,
                                           f->prev_nearest_depth.h, f->prev_nearest_depth.usage, FFX_RESOURCE_USAGE_UAV,
                                           L"KR_FSR3_PrevNearestDepth");
    static const float jscale = std::getenv("KARTRECOMP_FSR3_JSCALE") ? static_cast<float>(std::atof(std::getenv("KARTRECOMP_FSR3_JSCALE"))) : 1.0f;
    d.jitterOffset = {jscale * (Opt("jflipx") ? -fr->jitter_x : fr->jitter_x), jscale * (Opt("jflipy") ? -fr->jitter_y : fr->jitter_y)};
    d.motionVectorScale = {Opt("mvflipx") ? -1.0f : 1.0f, Opt("mvflipy") ? -1.0f : 1.0f};
    d.renderSize = {fr->render_width, fr->render_height};
    d.upscaleSize = {fr->output_width, fr->output_height};
    d.enableSharpening = fr->sharpness > 0.0f;
    d.sharpness = fr->sharpness;
    d.frameTimeDelta = fr->delta_time_ms > 0.0f ? fr->delta_time_ms : 16.6f;
    d.preExposure = fr->exposure > 0.0f ? fr->exposure : 1.0f;
    d.reset = fr->reset != 0;
    d.cameraNear = Param("near", fr->camera_near > 0.0f ? fr->camera_near : 2.0f);
    d.cameraFar = Opt("noinf") ? 100000.0f : FLT_MAX; // infinite far plane
    d.cameraFovAngleVertical = Param("fov", fr->camera_fov_y > 0.0f ? fr->camera_fov_y : 0.97f);
    d.viewSpaceToMetersFactor = Param("vs2m", 0.01f); // the game's units are centimetres
    d.flags = 0;
    const FfxErrorCode e = ffxFsr3UpscalerContextDispatch(&f->ctx, &d);
    if (e != FFX_OK) {
        std::fprintf(stderr, "[fsr3] dispatch failed (%d): render %ux%u (context max %ux%u), output %ux%u (context max %ux%u)\n",
                     static_cast<int>(e), fr->render_width, fr->render_height, f->rw, f->rh, fr->output_width, fr->output_height, f->ow, f->oh);
        return;
    }

    // The inputs end up in a read-only layout; the host wants everything back in GENERAL.
    std::vector<VkImageMemoryBarrier> back;
    const auto add_back = [&](VkImage image, VkImageAspectFlags aspect) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {aspect, 0, 1, 0, 1};
        back.push_back(b);
    };
    add_back(f->lin_in.image, VK_IMAGE_ASPECT_COLOR_BIT);
    add_back(f->reactive.image, VK_IMAGE_ASPECT_COLOR_BIT);
    add_back(f->reactive_final.image, VK_IMAGE_ASPECT_COLOR_BIT);
    if (have_reactive) {
        add_back(fr->opaque_color, VK_IMAGE_ASPECT_COLOR_BIT);
        add_back(fr->scene_color, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    add_back(fr->motion, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageAspectFlags depth_aspect = VK_IMAGE_ASPECT_DEPTH_BIT |
        ((depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    add_back(fr->depth, depth_aspect);
    // The shared images are declared in GENERAL every frame (as in AMD's sample); bring them back from whatever a pass left.
    add_back(f->dilated_depth.image, VK_IMAGE_ASPECT_COLOR_BIT);
    add_back(f->dilated_mv.image, VK_IMAGE_ASPECT_COLOR_BIT);
    add_back(f->prev_nearest_depth.image, VK_IMAGE_ASPECT_COLOR_BIT);
    f->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                   static_cast<uint32_t>(back.size()), back.data());

    // Linear RGBA16F -> the host's sRGB-encoded output image.
    compute_barrier();
    run(f->from_linear, f->set_out, f->out16_view, fr->output_view, fr->output_width, fr->output_height);
    compute_barrier();
}

void Destroy(void* instance) {
    auto* f = static_cast<Fsr3*>(instance);
    if (g_up == f) g_up = nullptr;
    Teardown(f);
    std::free(f->scratch);
    delete f;
}

const KrUpscalerApi kApi = {KR_UPSCALER_ABI_VERSION, "fsr3", &Create, &Configure, &Dispatch, &Destroy};

// ------------------------------------------------------------------------------------------------------ frame generation
// FSR 3 frame interpolation (AMD's FfxOpticalflow + FfxFrameInterpolation), run on the host's HUD-less output-size images. It
// shares the upscaler's dilated depth / motion vectors / previous nearest depth (written by the upscaler dispatch of the same
// frame), so it only works together with this library's upscaler; the host loads this library under both names
// (libkr_upscaler_fsr3.so and a libkr_framegen_fsr3.so symlink to it), which gives both the same instance of g_up.
struct Fg {
    KrUpscalerDevice dev{};
    PFN_vkDeviceWaitIdle wait_idle = nullptr;
    PFN_vkCmdPipelineBarrier cmd_barrier = nullptr;
    VkDeviceContext devctx{};
    void* scratch_fi = nullptr;
    void* scratch_of = nullptr;
    FfxFrameInterpolationContext fi{};
    FfxOpticalflowContext of{};
    FfxInterface be_fi{}, be_of{};
    bool have = false;
    Img of_vector, of_scd;
    uint32_t ow = 0, oh = 0, rw = 0, rh = 0;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    uint64_t last_frame = UINT64_MAX;
    bool fresh = true;
};

FfxSurfaceFormat FfxFormatOf(VkFormat f) {
    switch (f) {
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return FFX_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    default: return FFX_SURFACE_FORMAT_UNKNOWN;
    }
}

void FgTeardown(Fg* g) {
    if (g->have) {
        if (g->wait_idle) g->wait_idle(g->dev.device);
        ffxFrameInterpolationContextDestroy(&g->fi);
        ffxOpticalflowContextDestroy(&g->of);
        g->have = false;
    }
    // The optical-flow images are made with the upscaler's image helpers (its function table): free them through it.
    if (g_up) {
        for (Img* i : {&g->of_vector, &g->of_scd}) {
            if (i->image) g_up->destroy_image(g->dev.device, i->image, nullptr);
            if (i->memory) g_up->free_memory(g->dev.device, i->memory, nullptr);
            *i = {};
        }
    }
    g->fresh = true;
}

void* FgCreate(const KrUpscalerDevice* device) {
    auto* g = new Fg(); // the upscaler need not exist yet: Configure (at the first frame) looks for it
    g->dev = *device;
    g->wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(device->get_device_proc_addr(device->device, "vkDeviceWaitIdle"));
    g->cmd_barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(device->get_device_proc_addr(device->device, "vkCmdPipelineBarrier"));
    g_real_gdpa = device->get_device_proc_addr;
    g->devctx = {device->device, device->physical, &WrapGetDeviceProcAddr};
    return g;
}

int FgConfigure(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat color_format) {
    auto* g = static_cast<Fg*>(instance);
    FgTeardown(g);
    Fsr3* up = g_up;
    if (!up) {
        std::fprintf(stderr, "[fsr3 fg] needs the FSR 3 upscaler to be running\n");
        return 1;
    }
    const FfxSurfaceFormat format = FfxFormatOf(color_format);
    if (format == FFX_SURFACE_FORMAT_UNKNOWN) {
        std::fprintf(stderr, "[fsr3 fg] unsupported colour format %d\n", static_cast<int>(color_format));
        return 1;
    }
    const auto make_backend = [&](void*& scratch, FfxInterface& be, size_t contexts) {
        if (scratch) return true;
        const size_t size = ffxGetScratchMemorySizeVK(g->dev.physical, contexts);
        const size_t rounded = (size + 63) & ~size_t{63};
        scratch = std::aligned_alloc(64, rounded);
        if (!scratch) return false;
        std::memset(scratch, 0, rounded);
        return ffxGetInterfaceVK(&be, ffxGetDeviceVK(&g->devctx), scratch, size, contexts) == FFX_OK;
    };
    if (!make_backend(g->scratch_fi, g->be_fi, FFX_FRAMEINTERPOLATION_CONTEXT_COUNT + FFX_OPTICALFLOW_CONTEXT_COUNT)) {
        std::fprintf(stderr, "[fsr3 fg] backend interface failed\n");
        return 1;
    }
    FfxOpticalflowContextDescription od{};
    od.backendInterface = g->be_fi;
    od.resolution = {out_w, out_h};
    FfxErrorCode e = ffxOpticalflowContextCreate(&g->of, &od);
    if (e != FFX_OK) {
        std::fprintf(stderr, "[fsr3 fg] ffxOpticalflowContextCreate failed (%d)\n", static_cast<int>(e));
        return 1;
    }
    FfxFrameInterpolationContextDescription fd{};
    fd.backendInterface = g->be_fi;
    fd.flags = FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE; // as the upscaler: non-inverted depth, infinite far plane, LDR colour
    fd.maxRenderSize = {render_w, render_h};
    fd.displaySize = {out_w, out_h};
    fd.backBufferFormat = format;
    fd.previousInterpolationSourceFormat = format;
    e = ffxFrameInterpolationContextCreate(&g->fi, &fd);
    if (e != FFX_OK) {
        std::fprintf(stderr, "[fsr3 fg] ffxFrameInterpolationContextCreate failed (%d)\n", static_cast<int>(e));
        ffxOpticalflowContextDestroy(&g->of);
        return 1;
    }
    g->have = true;
    FfxOpticalflowSharedResourceDescriptions sd{};
    if (ffxOpticalflowGetSharedResourceDescriptions(&g->of, &sd) != FFX_OK) {
        std::fprintf(stderr, "[fsr3 fg] cannot query the optical-flow resources\n");
        return 1;
    }
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    struct { Img* img; const FfxCreateResourceDescription* d; } list[] = {{&g->of_vector, &sd.opticalFlowVector}, {&g->of_scd, &sd.opticalFlowSCD}};
    for (auto& item : list) {
        const VkFormat fmt = VkFormatOf(item.d->resourceDescription.format);
        if (fmt == VK_FORMAT_UNDEFINED ||
            !MakeImage(up, *item.img, fmt, item.d->resourceDescription.width, item.d->resourceDescription.height, usage)) {
            std::fprintf(stderr, "[fsr3 fg] cannot allocate an optical-flow image (format %d)\n",
                         static_cast<int>(item.d->resourceDescription.format));
            return 1;
        }
    }
    g->ow = out_w;
    g->oh = out_h;
    g->rw = render_w;
    g->rh = render_h;
    g->color_format = color_format;
    g->last_frame = UINT64_MAX;
    g->fresh = true;
    std::fprintf(stderr, "[fsr3 fg] FSR 3 frame interpolation: display %ux%u, render %ux%u\n", out_w, out_h, render_w, render_h);
    return 0;
}

int FgDispatch(void* instance, const KrFrameGenFrame* fr) {
    auto* g = static_cast<Fg*>(instance);
    Fsr3* up = g_up;
    if (!g->have || !up || !up->have_ctx || !up->dilated_depth.image || !fr->cur_color || !fr->output) return 1;
    if (g->fresh) {
        std::vector<VkImageMemoryBarrier> init;
        for (const Img* i : {&g->of_vector, &g->of_scd}) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = i->image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            init.push_back(b);
        }
        g->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                       static_cast<uint32_t>(init.size()), init.data());
        g->fresh = false;
    }
    // The interpolator keeps the previous frame itself: frames it did not see (menus, loading) mean it starts over.
    const bool consecutive = g->last_frame != UINT64_MAX && fr->frame_index == g->last_frame + 1;
    const bool reset = fr->reset != 0 || !consecutive;
    g->last_frame = fr->frame_index;

    const VkImageUsageFlags color_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const FfxResource color = Wrap(fr->cur_color, fr->color_format, fr->output_width, fr->output_height, color_usage,
                                   FFX_RESOURCE_USAGE_READ_ONLY, L"KR_FG_Color");
    const FfxResourceUsage shared_rt_uav = static_cast<FfxResourceUsage>(FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV);
    const FfxResource of_vector = Wrap(g->of_vector.image, g->of_vector.format, g->of_vector.w, g->of_vector.h, g->of_vector.usage,
                                       FFX_RESOURCE_USAGE_UAV, L"KR_FG_OFVector");
    const FfxResource of_scd = Wrap(g->of_scd.image, g->of_scd.format, g->of_scd.w, g->of_scd.h, g->of_scd.usage,
                                    FFX_RESOURCE_USAGE_UAV, L"KR_FG_OFSCD");
    FfxOpticalflowDispatchDescription od{};
    od.commandList = ffxGetCommandListVK(fr->cmd);
    od.color = color;
    od.opticalFlowVector = of_vector;
    od.opticalFlowSCD = of_scd;
    od.reset = reset;
    od.backbufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    od.minMaxLuminance = {0.0f, 1.0f};
    FfxErrorCode e = ffxOpticalflowContextDispatch(&g->of, &od);
    if (e != FFX_OK) {
        std::fprintf(stderr, "[fsr3 fg] optical flow dispatch failed (%d)\n", static_cast<int>(e));
        return 1;
    }
    FfxFrameInterpolationDispatchDescription fd{};
    fd.commandList = ffxGetCommandListVK(fr->cmd);
    fd.displaySize = {fr->output_width, fr->output_height};
    fd.renderSize = {fr->render_width, fr->render_height};
    fd.currentBackBuffer = color;
    fd.currentBackBuffer_HUDLess = color;
    fd.output = Wrap(fr->output, VK_FORMAT_R16G16B16A16_SFLOAT, fr->output_width, fr->output_height,
                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, FFX_RESOURCE_USAGE_UAV,
                     L"KR_FG_Output");
    fd.interpolationRect = {0, 0, static_cast<int32_t>(fr->output_width), static_cast<int32_t>(fr->output_height)};
    fd.opticalFlowVector = of_vector;
    fd.opticalFlowSceneChangeDetection = of_scd;
    fd.opticalFlowBufferSize = {g->of_vector.w, g->of_vector.h};
    fd.opticalFlowBlockSize = 8;
    fd.opticalFlowScale = {1.0f / static_cast<float>(fr->output_width), 1.0f / static_cast<float>(fr->output_height)};
    fd.cameraNear = fr->camera_near > 0.0f ? fr->camera_near : 2.0f;
    fd.cameraFar = FLT_MAX;
    fd.cameraFovAngleVertical = fr->camera_fov_y > 0.0f ? fr->camera_fov_y : 0.97f;
    fd.viewSpaceToMetersFactor = 0.01f; // the game's units are centimetres
    fd.frameTimeDelta = fr->delta_time_ms > 0.0f ? fr->delta_time_ms : 16.6f;
    fd.reset = reset;
    fd.backBufferTransferFunction = FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    fd.minMaxLuminance[0] = 0.0f;
    fd.minMaxLuminance[1] = 1.0f;
    fd.frameID = fr->frame_index;
    fd.dilatedDepth = Wrap(up->dilated_depth.image, up->dilated_depth.format, up->dilated_depth.w, up->dilated_depth.h,
                           up->dilated_depth.usage, shared_rt_uav, L"KR_FSR3_DilatedDepth");
    fd.dilatedMotionVectors = Wrap(up->dilated_mv.image, up->dilated_mv.format, up->dilated_mv.w, up->dilated_mv.h, up->dilated_mv.usage,
                                   shared_rt_uav, L"KR_FSR3_DilatedMotion");
    fd.reconstructedPrevDepth = Wrap(up->prev_nearest_depth.image, up->prev_nearest_depth.format, up->prev_nearest_depth.w,
                                     up->prev_nearest_depth.h, up->prev_nearest_depth.usage, FFX_RESOURCE_USAGE_UAV,
                                     L"KR_FSR3_PrevNearestDepth");
    e = ffxFrameInterpolationDispatch(&g->fi, &fd);
    if (e != FFX_OK) {
        std::fprintf(stderr, "[fsr3 fg] frame interpolation dispatch failed (%d)\n", static_cast<int>(e));
        return 1;
    }
    // Everything back to GENERAL, the layout the host expects.
    std::vector<VkImageMemoryBarrier> back;
    const auto add_back = [&](VkImage image) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        back.push_back(b);
    };
    add_back(fr->cur_color);
    add_back(up->dilated_depth.image);
    add_back(up->dilated_mv.image);
    add_back(up->prev_nearest_depth.image);
    add_back(g->of_vector.image);
    add_back(g->of_scd.image);
    g->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                   static_cast<uint32_t>(back.size()), back.data());
    return 0;
}

void FgDestroy(void* instance) {
    auto* g = static_cast<Fg*>(instance);
    FgTeardown(g);
    std::free(g->scratch_fi);
    delete g;
}

const KrFrameGenApi kFgApi = {KR_FRAMEGEN_ABI_VERSION, "fsr3", &FgCreate, &FgConfigure, &FgDispatch, &FgDestroy};


} // namespace

// The backend's interface table points at the frame-interpolation swapchain's configuration entry; this plugin drives frame
// interpolation itself (the host owns the swapchain), so it is a stub.
FFX_API FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(FfxFrameGenerationConfig const*) {
    return FFX_ERROR_INVALID_ARGUMENT;
}

extern "C" __attribute__((visibility("default"))) const KrUpscalerApi* kr_upscaler_get_api(void) {
    return &kApi;
}

extern "C" __attribute__((visibility("default"))) const KrFrameGenApi* kr_framegen_get_api(void) {
    return &kFgApi;
}
