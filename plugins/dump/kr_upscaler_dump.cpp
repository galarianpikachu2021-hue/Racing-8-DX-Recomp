// SPDX-License-Identifier: GPL-3.0-or-later
// kart-recomp: test plugin that forwards to another upscaler plugin (default: fsr3) and, for a range of frames, dumps what the
// upscaler was given (colour, depth, motion vectors, jitter ...) and what it produced, so other upscalers can be run offline on
// exactly the same inputs (see tools/fsr4-test/). Loaded as KARTRECOMP_UPSCALER=dump.
//
//   KARTRECOMP_DUMP_DIR=<dir>          output folder (required)
//   KARTRECOMP_DUMP_START=<n>          first temporal frame to dump (counted in upscaler dispatches, default 600)
//   KARTRECOMP_DUMP_COUNT=<n>          frames to dump (default 90)
//   KARTRECOMP_DUMP_INNER=<path>       the plugin to forward to (default plugins/libkr_upscaler_fsr3.so)
//
// Files: frame_NNNN.bin (header, colour, depth, motion, mismatch mask (R8)) and out_NNNN.raw (the inner plugin's output image, host format).
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "kr_upscaler_abi.h"

namespace {

struct Hdr {
    uint32_t magic = 0x5246524B; // 'KRFR'
    uint32_t version = 1;
    uint32_t rw, rh, ow, oh;
    float jitter_x, jitter_y, camera_near, camera_far, camera_fov_y, sharpness, delta_ms;
    uint32_t reset;
    uint32_t color_format, depth_format;
    uint64_t frame_index;
};

struct HostBuf {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* map = nullptr;
    size_t size = 0;
};

struct Dump {
    KrUpscalerDevice dev{};
    void* lib = nullptr;
    const KrUpscalerApi* inner = nullptr;
    void* inner_instance = nullptr;
    std::string dir;
    uint64_t start = 600, count = 90, seen = 0;
    uint32_t written = 0;

    PFN_vkGetPhysicalDeviceMemoryProperties get_mem_props = nullptr;
    PFN_vkCreateBuffer create_buffer = nullptr;
    PFN_vkGetBufferMemoryRequirements get_reqs = nullptr;
    PFN_vkAllocateMemory alloc_memory = nullptr;
    PFN_vkBindBufferMemory bind_memory = nullptr;
    PFN_vkMapMemory map_memory = nullptr;
    PFN_vkCmdPipelineBarrier cmd_barrier = nullptr;
    PFN_vkCmdCopyImageToBuffer cmd_copy = nullptr;

    uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    HostBuf color, depth, motion, mismatch, out;
    bool pending = false;
    Hdr pending_hdr{};
    uint32_t pending_index = 0;
};

bool MakeBuf(Dump* d, HostBuf& b, size_t size) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (d->create_buffer(d->dev.device, &ci, nullptr, &b.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    d->get_reqs(d->dev.device, b.buffer, &req);
    VkPhysicalDeviceMemoryProperties mp;
    d->get_mem_props(d->dev.physical, &mp);
    uint32_t type = UINT32_MAX;
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    for (int pass = 0; pass < 2 && type == UINT32_MAX; ++pass) {
        const VkMemoryPropertyFlags need = pass == 0 ? want : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & need) == need) {
                type = i;
                break;
            }
        }
    }
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (d->alloc_memory(d->dev.device, &mai, nullptr, &b.memory) != VK_SUCCESS) return false;
    d->bind_memory(d->dev.device, b.buffer, b.memory, 0);
    if (d->map_memory(d->dev.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.map) != VK_SUCCESS) return false;
    b.size = size;
    return true;
}

void WriteFile(const std::string& path, const std::vector<std::pair<const void*, size_t>>& parts) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    for (const auto& [p, n] : parts) std::fwrite(p, 1, n, f);
    std::fclose(f);
}

void FlushPending(Dump* d) {
    if (!d->pending) return;
    d->pending = false;
    char name[64];
    std::snprintf(name, sizeof(name), "/frame_%04u.bin", d->pending_index);
    WriteFile(d->dir + name, {{&d->pending_hdr, sizeof(Hdr)}, {d->color.map, d->color.size}, {d->depth.map, d->depth.size},
                              {d->motion.map, d->motion.size}, {d->mismatch.map, d->mismatch.size}});
    std::snprintf(name, sizeof(name), "/out_%04u.raw", d->pending_index);
    WriteFile(d->dir + name, {{d->out.map, d->out.size}});
}

void* Create(const KrUpscalerDevice* device) {
    auto* d = new Dump();
    d->dev = *device;
    const char* dir = std::getenv("KARTRECOMP_DUMP_DIR");
    d->dir = dir ? dir : "dump";
    if (const char* v = std::getenv("KARTRECOMP_DUMP_START")) d->start = std::strtoull(v, nullptr, 10);
    if (const char* v = std::getenv("KARTRECOMP_DUMP_COUNT")) d->count = std::strtoull(v, nullptr, 10);
    const char* inner_path = std::getenv("KARTRECOMP_DUMP_INNER");
    d->lib = dlopen(inner_path ? inner_path : "plugins/libkr_upscaler_fsr3.so", RTLD_NOW | RTLD_LOCAL);
    if (!d->lib) {
        std::fprintf(stderr, "[dump] cannot load the inner plugin: %s\n", dlerror());
        delete d;
        return nullptr;
    }
    auto get = reinterpret_cast<const KrUpscalerApi* (*)()>(dlsym(d->lib, "kr_upscaler_get_api"));
    d->inner = get ? get() : nullptr;
    d->inner_instance = d->inner ? d->inner->create(device) : nullptr;
    if (!d->inner_instance) {
        delete d;
        return nullptr;
    }
    const auto gipa = device->get_instance_proc_addr;
    const auto gdpa = device->get_device_proc_addr;
    d->get_mem_props = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(device->instance, "vkGetPhysicalDeviceMemoryProperties"));
#define LOAD(member, name) d->member = reinterpret_cast<PFN_##name>(gdpa(device->device, #name))
    LOAD(create_buffer, vkCreateBuffer);
    LOAD(get_reqs, vkGetBufferMemoryRequirements);
    LOAD(alloc_memory, vkAllocateMemory);
    LOAD(bind_memory, vkBindBufferMemory);
    LOAD(map_memory, vkMapMemory);
    LOAD(cmd_barrier, vkCmdPipelineBarrier);
    LOAD(cmd_copy, vkCmdCopyImageToBuffer);
#undef LOAD
    std::fprintf(stderr, "[dump] forwarding to '%s', dumping %llu frames from dispatch %llu into %s\n", d->inner->name,
                 static_cast<unsigned long long>(d->count), static_cast<unsigned long long>(d->start), d->dir.c_str());
    return d;
}

int Configure(void* instance, uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh, VkFormat fmt) {
    auto* d = static_cast<Dump*>(instance);
    const int r = d->inner->configure(d->inner_instance, rw, rh, ow, oh, fmt);
    if (r != 0) return r;
    if (rw != d->rw || rh != d->rh || ow != d->ow || oh != d->oh) {
        d->rw = rw;
        d->rh = rh;
        d->ow = ow;
        d->oh = oh;
        // Buffers are small enough to just leak on a size change (it happens a handful of times per run).
        d->color = d->depth = d->motion = d->mismatch = d->out = {};
        if (!MakeBuf(d, d->color, size_t{rw} * rh * 4) || !MakeBuf(d, d->depth, size_t{rw} * rh * 4) ||
            !MakeBuf(d, d->motion, size_t{rw} * rh * 4) || !MakeBuf(d, d->mismatch, size_t{rw} * rh) ||
            !MakeBuf(d, d->out, size_t{ow} * oh * 4)) {
            std::fprintf(stderr, "[dump] cannot allocate the readback buffers\n");
            return 1;
        }
    }
    return 0;
}

void Dispatch(void* instance, const KrUpscalerFrame* fr) {
    auto* d = static_cast<Dump*>(instance);
    FlushPending(d); // the previous frame's copies are finished (the host waits for its fence before recording)
    d->inner->dispatch(d->inner_instance, fr);
    if (!fr->depth) return; // menus: nothing temporal happened
    ++d->seen;
    if (d->seen <= d->start || d->written >= d->count) return;

    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    d->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    const auto copy = [&](VkImage image, VkImageAspectFlags aspect, const HostBuf& buf, uint32_t w, uint32_t h) {
        VkBufferImageCopy c{};
        c.imageSubresource = {aspect, 0, 0, 1};
        c.imageExtent = {w, h, 1};
        d->cmd_copy(fr->cmd, image, VK_IMAGE_LAYOUT_GENERAL, buf.buffer, 1, &c);
    };
    copy(fr->color, VK_IMAGE_ASPECT_COLOR_BIT, d->color, fr->render_width, fr->render_height);
    copy(fr->depth, VK_IMAGE_ASPECT_DEPTH_BIT, d->depth, fr->render_width, fr->render_height);
    copy(fr->motion, VK_IMAGE_ASPECT_COLOR_BIT, d->motion, fr->render_width, fr->render_height);
    copy(fr->output, VK_IMAGE_ASPECT_COLOR_BIT, d->out, fr->output_width, fr->output_height);
    if (fr->mismatch_mask) copy(fr->mismatch_mask, VK_IMAGE_ASPECT_COLOR_BIT, d->mismatch, fr->render_width, fr->render_height);
    else std::memset(d->mismatch.map, 0, d->mismatch.size);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    d->cmd_barrier(fr->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);

    Hdr h;
    h.rw = fr->render_width;
    h.rh = fr->render_height;
    h.ow = fr->output_width;
    h.oh = fr->output_height;
    h.jitter_x = fr->jitter_x;
    h.jitter_y = fr->jitter_y;
    h.camera_near = fr->camera_near;
    h.camera_far = fr->camera_far;
    h.camera_fov_y = fr->camera_fov_y;
    h.sharpness = fr->sharpness;
    h.delta_ms = fr->delta_time_ms;
    h.reset = fr->reset;
    h.color_format = static_cast<uint32_t>(fr->color_format);
    h.depth_format = static_cast<uint32_t>(fr->depth_format);
    h.frame_index = fr->frame_index;
    d->pending_hdr = h;
    d->pending_index = d->written++;
    d->pending = true;
}

void Destroy(void* instance) {
    auto* d = static_cast<Dump*>(instance);
    FlushPending(d);
    if (d->inner && d->inner_instance) d->inner->destroy(d->inner_instance);
    delete d;
}

const KrUpscalerApi kApi = {KR_UPSCALER_ABI_VERSION, "dump", &Create, &Configure, &Dispatch, &Destroy};

} // namespace

extern "C" __attribute__((visibility("default"))) const KrUpscalerApi* kr_upscaler_get_api(void) {
    return &kApi;
}
