// SPDX-License-Identifier: GPL-3.0-or-later
// PS5X360E (alpha.92): binds the functions of mihawkgen_dispatch.hpp.
#include "mihawkgen_dispatch.hpp"

namespace ps5x360::mihawk_dispatch {
bool Bind(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr get_instance_proc,
          const char** missing) {
  const char* none = nullptr;
  if (!missing) missing = &none;
  if (!get_instance_proc) { *missing = "vkGetInstanceProcAddr"; return false; }
  vkGetDeviceProcAddr =
      reinterpret_cast<PFN_vkGetDeviceProcAddr>(get_instance_proc(instance, "vkGetDeviceProcAddr"));
  if (!vkGetDeviceProcAddr) { *missing = "vkGetDeviceProcAddr"; return false; }
#define MIHAWK_DEVICE(name)                                                         \
  name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));          \
  if (!name) { *missing = #name; return false; }
#define MIHAWK_INSTANCE(name)                                                       \
  name = reinterpret_cast<PFN_##name>(get_instance_proc(instance, #name));          \
  if (!name) { *missing = #name; return false; }
  MIHAWK_DEVICE(vkAllocateDescriptorSets)
  MIHAWK_DEVICE(vkAllocateMemory)
  MIHAWK_DEVICE(vkBindBufferMemory)
  MIHAWK_DEVICE(vkBindImageMemory)
  MIHAWK_DEVICE(vkCmdBindDescriptorSets)
  MIHAWK_DEVICE(vkCmdBindPipeline)
  MIHAWK_DEVICE(vkCmdClearColorImage)
  MIHAWK_DEVICE(vkCmdDispatch)
  MIHAWK_DEVICE(vkCmdPipelineBarrier)
  MIHAWK_DEVICE(vkCmdPushConstants)
  MIHAWK_DEVICE(vkCmdResetQueryPool)
  MIHAWK_DEVICE(vkCmdWriteTimestamp)
  MIHAWK_DEVICE(vkCreateBuffer)
  MIHAWK_DEVICE(vkCreateComputePipelines)
  MIHAWK_DEVICE(vkCreateDescriptorPool)
  MIHAWK_DEVICE(vkCreateDescriptorSetLayout)
  MIHAWK_DEVICE(vkCreateImage)
  MIHAWK_DEVICE(vkCreateImageView)
  MIHAWK_DEVICE(vkCreatePipelineLayout)
  MIHAWK_DEVICE(vkCreateQueryPool)
  MIHAWK_DEVICE(vkCreateSampler)
  MIHAWK_DEVICE(vkCreateShaderModule)
  MIHAWK_DEVICE(vkDestroyBuffer)
  MIHAWK_DEVICE(vkDestroyDescriptorPool)
  MIHAWK_DEVICE(vkDestroyDescriptorSetLayout)
  MIHAWK_DEVICE(vkDestroyImage)
  MIHAWK_DEVICE(vkDestroyImageView)
  MIHAWK_DEVICE(vkDestroyPipeline)
  MIHAWK_DEVICE(vkDestroyPipelineLayout)
  MIHAWK_DEVICE(vkDestroyQueryPool)
  MIHAWK_DEVICE(vkDestroySampler)
  MIHAWK_DEVICE(vkDestroyShaderModule)
  MIHAWK_DEVICE(vkFreeMemory)
  MIHAWK_DEVICE(vkGetBufferMemoryRequirements)
  MIHAWK_DEVICE(vkGetImageMemoryRequirements)
  MIHAWK_DEVICE(vkGetQueryPoolResults)
  MIHAWK_DEVICE(vkMapMemory)
  MIHAWK_DEVICE(vkUnmapMemory)
  MIHAWK_DEVICE(vkUpdateDescriptorSets)
  MIHAWK_INSTANCE(vkGetPhysicalDeviceFormatProperties)
  MIHAWK_INSTANCE(vkGetPhysicalDeviceMemoryProperties)
  MIHAWK_INSTANCE(vkGetPhysicalDeviceProperties)
#undef MIHAWK_DEVICE
#undef MIHAWK_INSTANCE
  return true;
}
}  // namespace ps5x360::mihawk_dispatch
