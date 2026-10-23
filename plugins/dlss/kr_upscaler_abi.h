// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp upscaler plugin ABI: the C interface of suyu_cmd/native_gpu/upscaler.h in the suyu fork, kept here so plugins can
// be built without the fork. Keep it identical to that header (KR_UPSCALER_ABI_VERSION).
#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

extern "C" {

#define KR_UPSCALER_ABI_VERSION 1

typedef struct KrUpscalerDevice {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr;
} KrUpscalerDevice;

/// One frame. All images are in VK_IMAGE_LAYOUT_GENERAL and idle when Dispatch is
/// called; the plugin records into `cmd` (already recording, outside a render pass).
typedef struct KrUpscalerFrame {
    VkCommandBuffer cmd;
    VkImage color;        ///< HDR scene colour, render_width x render_height
    VkImageView color_view;
    VkFormat color_format;
    VkImage depth;        ///< scene depth (reverse-Z not assumed: see depth_inverted)
    VkImageView depth_view;
    VkFormat depth_format;  ///< VK_FORMAT_UNDEFINED when there is no depth
    /// The 3D scene's HDR colour (before post-processing) as it was before any alpha-blended draw, and the same colour after them,
    /// both at the render size; null when the frame had no alpha-blended 3D draws. Upscalers derive a "reactive mask" (where
    /// translucent effects make history untrustworthy) from the pair.
    VkImage opaque_color;
    VkImageView opaque_color_view;
    VkImage scene_color;
    VkImageView scene_color_view;
    VkFormat scene_color_format;
    /// R8, render size, 0..0.9: how little the camera-derived motion vector explains this pixel (animated characters, effects: no
    /// motion data of their own). A natural reactive mask; null when unavailable.
    VkImage mismatch_mask;
    VkImageView mismatch_mask_view;
    VkImage motion;       ///< RG16F motion vectors in pixels (prev = cur + mv); zeros = static
    VkImageView motion_view;
    VkImage output;       ///< upscaled colour, output_width x output_height, same format as color
    VkImageView output_view;
    uint32_t render_width, render_height;
    uint32_t output_width, output_height;
    float jitter_x, jitter_y;   ///< this frame's sub-pixel jitter, in render pixels
    float delta_time_ms;
    float camera_near, camera_far, camera_fov_y; ///< 0 when unknown
    float sharpness;            ///< 0..1
    float exposure;             ///< pre-exposure multiplier (1 = none)
    uint32_t reset;             ///< 1 after a cut: drop the history
    uint32_t depth_inverted;
    uint64_t frame_index;
} KrUpscalerFrame;

typedef struct KrUpscalerApi {
    uint32_t abi_version; ///< KR_UPSCALER_ABI_VERSION
    const char* name;
    /// Returns an opaque instance, or null when the GPU or driver can't run this backend.
    void* (*create)(const KrUpscalerDevice* device);
    /// (Re)configure for a size; returns 0 on success.
    int (*configure)(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h,
                     VkFormat color_format);
    void (*dispatch)(void* instance, const KrUpscalerFrame* frame);
    void (*destroy)(void* instance);
} KrUpscalerApi;

typedef const KrUpscalerApi* (*KrUpscalerGetApi)(void);

// ---------------------------------------------------------------------------------------------------------- frame generation
// A frame-generation backend (FSR 3 frame interpolation, FSR 4 / DLSS 4 where their SDKs allow it, or the host's built-in
// interpolator) is a plugin too: a library `plugins/libkr_framegen_<name>.so`, or any upscaler plugin, exporting
// `const KrFrameGenApi* kr_framegen_get_api(void)`. For each game frame the host hands it the previous and the current 3D image
// (output size, display-referred, HUD not drawn yet), the motion vectors and depth the upscaler used, the camera, and an output
// image to fill with the frame that goes between them. The host lays the HUD over the result, presents it, waits half a frame
// and presents the real frame.

#define KR_FRAMEGEN_ABI_VERSION 1

/// All images are in VK_IMAGE_LAYOUT_GENERAL and idle on entry; record into `cmd` (recording, outside a render pass) and leave
/// everything in GENERAL.
typedef struct KrFrameGenFrame {
    VkCommandBuffer cmd;
    VkImage prev_color;        ///< previous frame's 3D image, output_width x output_height
    VkImageView prev_color_view;
    VkImage cur_color;         ///< this frame's 3D image, same size and format
    VkImageView cur_color_view;
    VkFormat color_format;
    VkImage depth;             ///< scene depth at the render size (null when there is none)
    VkImageView depth_view;
    VkFormat depth_format;
    VkImage motion;            ///< RG16F, render size: motion in render pixels, prev = cur + mv
    VkImageView motion_view;
    VkImage output;            ///< the generated frame: VK_FORMAT_R16G16B16A16_SFLOAT, output size, storage + sampled + transfer
    VkImageView output_view;
    uint32_t render_width, render_height;
    uint32_t output_width, output_height;
    float interpolation_t;     ///< where between prev (0) and cur (1) the frame sits; 0.5 for 2x
    float jitter_x, jitter_y;  ///< this frame's camera jitter in render pixels (already removed from color; for the backend's own use)
    float delta_time_ms;       ///< game frame time
    float camera_near, camera_far, camera_fov_y; ///< 0 when unknown
    float view_proj[16];       ///< this frame's view-projection rows (clip = row . (world, 1); game clip space, y up, depth 0..1), zeros when unknown
    float prev_view_proj[16];
    uint32_t reset;            ///< 1 after a cut: do not interpolate across it (the host skips such frames; set for backends that track state)
    uint32_t depth_inverted;
    uint64_t frame_index;
} KrFrameGenFrame;

typedef struct KrFrameGenApi {
    uint32_t abi_version; ///< KR_FRAMEGEN_ABI_VERSION
    const char* name;
    void* (*create)(const KrUpscalerDevice* device); ///< null when the GPU or driver can't run it
    int (*configure)(void* instance, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h, VkFormat color_format);
    /// Returns 0 when `output` was written; anything else makes the host fall back to presenting the real frame alone.
    int (*dispatch)(void* instance, const KrFrameGenFrame* frame);
    void (*destroy)(void* instance);
} KrFrameGenApi;

typedef const KrFrameGenApi* (*KrFrameGenGetApi)(void);

} // extern "C"
