// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp: replays frames dumped by plugins/dump through an upscaler plugin (libkr_upscaler_<name>.so) in a minimal Vulkan host, so a
// plugin can be iterated on in seconds and compared with other upscalers on exactly the same inputs.
//   replay_host <plugin.so> <dump dir> <out dir> [first] [count]      writes out_NNNN.raw (A2B10G10R10, output size)
#include <dlfcn.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "kr_upscaler_abi.h"

struct Hdr {
    uint32_t magic, version, rw, rh, ow, oh;
    float jitter_x, jitter_y, camera_near, camera_far, camera_fov_y, sharpness, delta_ms;
    uint32_t reset, color_format, depth_format;
    uint64_t frame_index;
};

#define VK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { std::fprintf(stderr, "%s failed: %d\n", #x, _r); std::exit(1); } } while (0)

static VkDevice dev;
static VkPhysicalDevice phys;
static uint32_t find_type(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    std::exit(2);
}
struct Image { VkImage image; VkDeviceMemory mem; VkImageView view; };
static Image make_image(VkFormat fmt, uint32_t w, uint32_t h, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image r{};
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt; ci.extent = {w, h, 1}; ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK(vkCreateImage(dev, &ci, nullptr, &r.image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, r.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = find_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(dev, &ai, nullptr, &r.mem)); vkBindImageMemory(dev, r.image, r.mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = r.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt; vi.subresourceRange = {aspect, 0, 1, 0, 1};
    VK(vkCreateImageView(dev, &vi, nullptr, &r.view));
    return r;
}
struct Buffer { VkBuffer buf; VkDeviceMemory mem; void* map; size_t size; };
static Buffer make_buffer(size_t size) {
    Buffer b{}; b.size = size;
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK(vkCreateBuffer(dev, &ci, nullptr, &b.buf));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, b.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size;
    ai.memoryTypeIndex = find_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(dev, &ai, nullptr, &b.mem)); vkBindBufferMemory(dev, b.buf, b.mem, 0);
    VK(vkMapMemory(dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map));
    return b;
}

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    const std::string dump = argv[2], out = argv[3];
    const int first = argc > 4 ? std::atoi(argv[4]) : 0, count = argc > 5 ? std::atoi(argv[5]) : 1000000;
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    auto get = reinterpret_cast<const KrUpscalerApi* (*)()>(dlsym(lib, "kr_upscaler_get_api"));
    const KrUpscalerApi* api = get();

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    VkInstance inst; VK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data());
    phys = pds[0];
    uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, nullptr); std::vector<VkQueueFamilyProperties> qf(qn); vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf.data());
    uint32_t family = 0; for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f11; f11.pNext = &f12; f12.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(phys, &f2);
    f2.features.robustBufferAccess = VK_FALSE; f13.robustImageAccess = VK_FALSE; // the host's settings
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext = &f2; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    VK(vkCreateDevice(phys, &dci, nullptr, &dev));
    VkQueue queue; vkGetDeviceQueue(dev, family, 0, &queue);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = family;
    VkCommandPool pool; VK(vkCreateCommandPool(dev, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd; VK(vkAllocateCommandBuffers(dev, &cai, &cmd));

    KrUpscalerDevice kd{inst, phys, dev, queue, family, vkGetInstanceProcAddr, vkGetDeviceProcAddr};
    void* inst_p = api->create(&kd);
    if (!inst_p) return 1;

    std::string path = dump + "/frame_0000.bin";
    FILE* f0 = std::fopen(path.c_str(), "rb"); if (!f0) return 1;
    Hdr h0; std::fread(&h0, sizeof(h0), 1, f0); std::fclose(f0);
    const uint32_t rw = h0.rw, rh = h0.rh, ow = h0.ow, oh = h0.oh;
    if (api->configure(inst_p, rw, rh, ow, oh, VK_FORMAT_A2B10G10R10_UNORM_PACK32) != 0) return 1;
    Image mismatch = make_image(VK_FORMAT_R8_UNORM, rw, rh, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Image color = make_image(VK_FORMAT_A2B10G10R10_UNORM_PACK32, rw, rh, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Image depth = make_image(VK_FORMAT_D32_SFLOAT, rw, rh, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    Image motion = make_image(VK_FORMAT_R16G16_SFLOAT, rw, rh, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Image output = make_image(VK_FORMAT_A2B10G10R10_UNORM_PACK32, ow, oh, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    Buffer in = make_buffer(size_t{rw} * rh * 13), outb = make_buffer(size_t{ow} * oh * 4);

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; vkCreateFence(dev, &fci, nullptr, &fence);
    bool layouts_set = false;
    std::vector<uint8_t> raw(size_t{rw} * rh * 12);
    for (int i = first; i < first + count; ++i) {
        char name[64]; std::snprintf(name, sizeof(name), "/frame_%04d.bin", i);
        FILE* f = std::fopen((dump + name).c_str(), "rb"); if (!f) break;
        Hdr h; std::fread(&h, sizeof(h), 1, f); std::memset(in.map, 0, in.size); std::fread(in.map, 1, in.size, f); std::fclose(f);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkResetCommandBuffer(cmd, 0); vkBeginCommandBuffer(cmd, &bi);
        const auto barrier = [&](VkImage im, VkImageAspectFlags asp, VkImageLayout from, VkImageLayout to) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = im; b.subresourceRange = {asp, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        const VkImageLayout from = layouts_set ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier(color.image, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_GENERAL);
        barrier(depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, from, VK_IMAGE_LAYOUT_GENERAL);
        barrier(motion.image, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_GENERAL);
        barrier(mismatch.image, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_GENERAL);
        if (!layouts_set) barrier(output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        layouts_set = true;
        VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {rw, rh, 1};
        c.bufferOffset = 0; vkCmdCopyBufferToImage(cmd, in.buf, color.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; c.bufferOffset = size_t{rw} * rh * 4; vkCmdCopyBufferToImage(cmd, in.buf, depth.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.bufferOffset = size_t{rw} * rh * 8; vkCmdCopyBufferToImage(cmd, in.buf, motion.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        c.bufferOffset = size_t{rw} * rh * 12; vkCmdCopyBufferToImage(cmd, in.buf, mismatch.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

        KrUpscalerFrame fr{};
        fr.cmd = cmd; fr.color = color.image; fr.color_view = color.view; fr.color_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        fr.depth = depth.image; fr.depth_view = depth.view; fr.depth_format = VK_FORMAT_D32_SFLOAT; fr.motion = motion.image; fr.motion_view = motion.view;
        fr.output = output.image; fr.output_view = output.view;
        if (!std::getenv("REPLAY_NOMISMATCH")) { fr.mismatch_mask = mismatch.image; fr.mismatch_mask_view = mismatch.view; }
        fr.render_width = rw; fr.render_height = rh; fr.output_width = ow; fr.output_height = oh;
        fr.jitter_x = h.jitter_x; fr.jitter_y = h.jitter_y; fr.delta_time_ms = h.delta_ms; fr.camera_near = h.camera_near; fr.camera_far = h.camera_far;
        fr.camera_fov_y = h.camera_fov_y; fr.sharpness = std::getenv("REPLAY_SHARPNESS") ? static_cast<float>(std::atof(std::getenv("REPLAY_SHARPNESS"))) : 0.0f;
        fr.exposure = 1.0f; fr.reset = (i == first) || h.reset; fr.frame_index = h.frame_index;
        api->dispatch(inst_p, &fr);

        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        VkBufferImageCopy oc{}; oc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; oc.imageExtent = {ow, oh, 1};
        vkCmdCopyImageToBuffer(cmd, output.image, VK_IMAGE_LAYOUT_GENERAL, outb.buf, 1, &oc);
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
        vkResetFences(dev, 1, &fence); VK(vkQueueSubmit(queue, 1, &si, fence)); VK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
        std::snprintf(name, sizeof(name), "/out_%04d.raw", i);
        FILE* fo = std::fopen((out + name).c_str(), "wb"); std::fwrite(outb.map, 1, outb.size, fo); std::fclose(fo);
    }
    api->destroy(inst_p);
    return 0;
}
