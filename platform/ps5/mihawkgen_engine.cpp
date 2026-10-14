// SPDX-License-Identifier: GPL-3.0-or-later
// PS5X360E (alpha.92): MihawkGen as a frame generation engine of the presenter
// (xenia/ui/vulkan/framegen/mihawk_engine.hpp states the interface). The
// library (third_party/mihawkgen, unchanged) makes the picture at a time t
// between two of the game's frames with its trained network
// (FG_METHOD_NEURAL); this file keeps the two frames and the output the
// presenter copies into and paints from, puts them in the layouts the library
// asks for around each call, and times the calls on the GPU.
//
// The network's weights (third_party/mihawkgen/framegen/models/network.fgw,
// 1.7 MB) are part of the title: the assembler takes the file in below.
// No prototypes: every vk* name below is a function bound at run time.
#define VK_NO_PROTOTYPES
#include "xenia/ui/vulkan/framegen/mihawk_engine.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "mihawkgen_vk.hpp"
#include "../../third_party/mihawkgen/framegen/include/framegen.h"

#ifndef MIHAWKGEN_NETWORK
#error "MIHAWKGEN_NETWORK: the path of third_party/mihawkgen/framegen/models/network.fgw (canary/CMakeLists.txt)"
#endif
extern "C" const unsigned char ps5x360e_mihawkgen_network_start[];
extern "C" const unsigned char ps5x360e_mihawkgen_network_end[];
__asm__(
    ".section .rodata.ps5x360e_mihawkgen_network,\"a\",@progbits\n"
    ".balign 16\n"
    ".global ps5x360e_mihawkgen_network_start\n"
    ".hidden ps5x360e_mihawkgen_network_start\n"
    "ps5x360e_mihawkgen_network_start:\n"
    ".incbin \"" MIHAWKGEN_NETWORK "\"\n"
    ".global ps5x360e_mihawkgen_network_end\n"
    ".hidden ps5x360e_mihawkgen_network_end\n"
    "ps5x360e_mihawkgen_network_end:\n"
    ".previous\n");

namespace ps5x360::mihawk {
namespace {

// Frames in flight for the library's per-slot images and descriptor rings. The
// presenter waits for its earlier paints before each new frame, so this is
// headroom; the barriers below order the reuse either way.
constexpr uint32_t kSlots = 3;
// Timestamps: one pair per generated picture (up to 3) per slot.
constexpr uint32_t kPicturesMax = 3;
constexpr uint32_t kQueriesPerSlot = kPicturesMax * 2;

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

class EngineImpl final : public Engine {
 public:
  ~EngineImpl() override { Destroy(); }

  bool Create(VkPhysicalDevice physical, VkDevice device, uint32_t width, uint32_t height,
              VkFormat frame_format, std::string& error) {
    physical_ = physical;
    device_ = device;
    width_ = width;
    height_ = height;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_);
    // The frames: copied into (or blitted, for a smaller copy), sampled by the
    // network and by the presenter's paint, and copied from.
    for (unsigned i = 0; i < 2; ++i) {
      if (!MakeImage(frame_format,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                         VK_IMAGE_USAGE_SAMPLED_BIT,
                     frames_[i], error)) {
        return false;
      }
    }
    if (!MakeImage(k_format,
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   output_, error)) {
      return false;
    }
    fg_create_info info{};
    info.device = device;
    info.physical_device = physical;
    info.width = width;
    info.height = height;
    info.slots = kSlots;
    info.output_format = k_format;
    char text[256] = {};
    VkResult result = fg_context_create(&info, &context_, text, sizeof(text));
    if (result != VK_SUCCESS) {
      error = std::string("MihawkGen context: ") + text;
      return false;
    }
    const size_t network_size =
        size_t(ps5x360e_mihawkgen_network_end - ps5x360e_mihawkgen_network_start);
    result = fg_context_load_network(context_, ps5x360e_mihawkgen_network_start, network_size, text,
                                     sizeof(text));
    if (result != VK_SUCCESS || !fg_method_available(context_, FG_METHOD_NEURAL)) {
      error = std::string("MihawkGen network (") + std::to_string(network_size) + " bytes): " + text;
      return false;
    }
    // GPU time: without timestamps the engine works, unmeasured.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical, &properties);
    timestamp_period_ns_ = properties.limits.timestampPeriod;
    if (timestamp_period_ns_ > 0.0f) {
      VkQueryPoolCreateInfo pool{};
      pool.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      pool.queryType = VK_QUERY_TYPE_TIMESTAMP;
      pool.queryCount = kSlots * kQueriesPerSlot;
      if (vkCreateQueryPool(device, &pool, nullptr, &queries_) != VK_SUCCESS) {
        queries_ = VK_NULL_HANDLE;
      }
    }
    return true;
  }

  VkImage snapshot_image(unsigned i) const override { return frames_[i & 1].image; }
  VkImageView snapshot_view(unsigned i) const override { return frames_[i & 1].view; }
  VkImage output_image() const override { return output_.image; }
  VkImageView output_view() const override { return output_.view; }
  VkImage frame_image() const override { return frames_[frames_count_ & 1].image; }

  void prepare(VkCommandBuffer command) override {
    if (prepared_) return;
    // Every image to GENERAL, once.
    VkImageMemoryBarrier barriers[3]{};
    const Image* images[3] = {&frames_[0], &frames_[1], &output_};
    for (uint32_t n = 0; n < 3; ++n) {
      VkImageMemoryBarrier& b = barriers[n];
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask = 0;
      b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = images[n]->image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 3, barriers);
    prepared_ = true;
  }

  void set_generated(uint32_t n) override { generated_ = std::clamp<uint32_t>(n, 1, kPicturesMax); }
  uint32_t generated() const override { return generated_; }

  bool record(VkCommandBuffer command, bool reset) override {
    last_made_ = false;
    if (!context_) return false;
    const uint32_t slot = uint32_t(frames_count_ % kSlots);
    last_slot_ = slot;
    if (queries_) {
      // What this slot measured kSlots frames ago, then its queries afresh.
      CollectTimes(slot);
      vkCmdResetQueryPool(command, queries_, slot * kQueriesPerSlot, kQueriesPerSlot);
      slot_pictures_[slot] = 0;
    }
    // The network needs no history: the first frame, or one after a cut, has
    // only nothing to be the picture before.
    if (reset) return false;
    Generate(command, slot, 0, 1.0f / float(generated_ + 1));
    last_made_ = true;
    return true;
  }

  bool interpolate_next(VkCommandBuffer command, uint32_t i) override {
    if (!last_made_ || i == 0 || i >= generated_) return false;
    Generate(command, last_slot_, i, float(i + 1) / float(generated_ + 1));
    return true;
  }

  void advance() override { ++frames_count_; }

  bool take_gpu_time(double& mean_ms, double& max_ms, uint32_t& count) override {
    if (!timed_frames_) return false;
    mean_ms = timed_ns_ / 1e6 / double(timed_frames_);
    max_ms = timed_max_ns_ / 1e6;
    count = timed_frames_;
    timed_ns_ = timed_max_ns_ = 0;
    timed_frames_ = 0;
    return true;
  }

 private:
  uint32_t FindMemory(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < memory_.memoryTypeCount; ++i) {
      if ((bits & (1u << i)) && (memory_.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return UINT32_MAX;
  }

  bool MakeImage(VkFormat format, VkImageUsageFlags usage, Image& out, std::string& error) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width_, height_, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &info, nullptr, &out.image) != VK_SUCCESS) {
      error = "MihawkGen: vkCreateImage failed";
      return false;
    }
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, out.image, &requirements);
    uint32_t type = FindMemory(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) type = FindMemory(requirements.memoryTypeBits, 0);
    VkMemoryAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (type == UINT32_MAX || vkAllocateMemory(device_, &allocate, nullptr, &out.memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, out.image, out.memory, 0) != VK_SUCCESS) {
      error = "MihawkGen: no memory for an image";
      return false;
    }
    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = out.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &view, nullptr, &out.view) != VK_SUCCESS) {
      error = "MihawkGen: vkCreateImageView failed";
      return false;
    }
    return true;
  }

  void Destroy() {
    if (!device_) return;
    if (context_) fg_context_destroy(context_);
    context_ = nullptr;
    if (queries_) vkDestroyQueryPool(device_, queries_, nullptr);
    queries_ = VK_NULL_HANDLE;
    for (Image* image : {&frames_[0], &frames_[1], &output_}) {
      if (image->view) vkDestroyImageView(device_, image->view, nullptr);
      if (image->image) vkDestroyImage(device_, image->image, nullptr);
      if (image->memory) vkFreeMemory(device_, image->memory, nullptr);
      *image = {};
    }
    device_ = VK_NULL_HANDLE;
  }

  // One picture at `t` into the output, between the frame before (t = 0) and
  // this one (t = 1).
  void Generate(VkCommandBuffer command, uint32_t slot, uint32_t picture, float t) {
    const unsigned current = unsigned(frames_count_ & 1), previous = current ^ 1;
    // The library samples the frames in SHADER_READ_ONLY_OPTIMAL: from GENERAL,
    // after the caller's copy into the current one and the paints of both; and
    // writes the output after the paint of the picture before has read it.
    VkImageMemoryBarrier in[3]{};
    for (unsigned n = 0; n < 2; ++n) {
      VkImageMemoryBarrier& b = in[n];
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
      b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
      b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = frames_[n].image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    VkImageMemoryBarrier& o = in[2];
    o.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    o.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    o.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    o.oldLayout = o.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    o.srcQueueFamilyIndex = o.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    o.image = output_.image;
    o.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // The library's own images of this slot (and its descriptor ring) were
    // last used by earlier work: all of it before these passes.
    VkMemoryBarrier earlier{};
    earlier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    earlier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    earlier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &earlier, 0, nullptr, 3, in);
    const bool timed = queries_ && picture < kPicturesMax;
    if (timed) {
      vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries_,
                          slot * kQueriesPerSlot + picture * 2);
    }
    fg_frame_pair pair{};
    pair.prev_color = frames_[previous].view;
    pair.next_color = frames_[current].view;
    pair.t = t;
    fg_record_generate(context_, command, slot, FG_METHOD_NEURAL, &pair, output_.view);
    if (timed) {
      vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_,
                          slot * kQueriesPerSlot + picture * 2 + 1);
      slot_pictures_[slot] = std::max(slot_pictures_[slot], picture + 1);
    }
    // Back for the caller: the output for its copies and paints, the frames to
    // GENERAL for its next copy and its paints.
    VkImageMemoryBarrier out[3]{};
    for (unsigned n = 0; n < 2; ++n) {
      VkImageMemoryBarrier& b = out[n];
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
      b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = frames_[n].image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    VkImageMemoryBarrier& made = out[2];
    made.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    made.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    made.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    made.oldLayout = made.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    made.srcQueueFamilyIndex = made.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    made.image = output_.image;
    made.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 3, out);
  }

  // The pictures this slot made when it was last used, if the GPU is done
  // with them (never waiting).
  void CollectTimes(uint32_t slot) {
    const uint32_t pictures = slot_pictures_[slot];
    if (!pictures) return;
    uint64_t values[kQueriesPerSlot * 2] = {};
    const VkResult result = vkGetQueryPoolResults(
        device_, queries_, slot * kQueriesPerSlot, pictures * 2, sizeof(values), values,
        sizeof(uint64_t) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (result != VK_SUCCESS && result != VK_NOT_READY) return;
    double frame_ns = 0;
    for (uint32_t p = 0; p < pictures; ++p) {
      const uint64_t* begin = &values[(p * 2) * 2];
      const uint64_t* end = &values[(p * 2 + 1) * 2];
      if (!begin[1] || !end[1] || end[0] < begin[0]) return;  // not available yet
      frame_ns += double(end[0] - begin[0]) * double(timestamp_period_ns_);
    }
    timed_ns_ += frame_ns;
    timed_max_ns_ = std::max(timed_max_ns_, frame_ns);
    ++timed_frames_;
  }

  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memory_{};
  uint32_t width_ = 0, height_ = 0;
  Image frames_[2]{};
  Image output_{};
  fg_context* context_ = nullptr;
  uint64_t frames_count_ = 0;
  uint32_t generated_ = 1;
  bool prepared_ = false;
  bool last_made_ = false;
  uint32_t last_slot_ = 0;
  VkQueryPool queries_ = VK_NULL_HANDLE;
  float timestamp_period_ns_ = 0.0f;
  uint32_t slot_pictures_[kSlots] = {};
  double timed_ns_ = 0, timed_max_ns_ = 0;
  uint32_t timed_frames_ = 0;
};

}  // namespace

std::unique_ptr<Engine> CreateEngine(VkInstance instance, PFN_vkGetInstanceProcAddr get_instance_proc,
                                     VkPhysicalDevice physical_device, VkDevice device, uint32_t width,
                                     uint32_t height, VkFormat frame_format, std::string& error) {
  const char* missing = nullptr;
  if (!mihawk_dispatch::Bind(instance, device, get_instance_proc, &missing)) {
    error = std::string("MihawkGen: no ") + (missing ? missing : "Vulkan function");
    return nullptr;
  }
  auto engine = std::make_unique<EngineImpl>();
  if (!engine->Create(physical_device, device, width, height, frame_format, error)) return nullptr;
  return engine;
}

}  // namespace ps5x360::mihawk
