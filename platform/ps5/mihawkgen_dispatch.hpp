// SPDX-License-Identifier: GPL-3.0-or-later
// PS5X360E (alpha.92): the Vulkan functions MihawkGen (third_party/mihawkgen)
// and its engine (mihawkgen_engine.cpp) call, bound at run time from the
// emulator's instance and device (the title links no Vulkan loader, and the
// driver exports no vk* symbols). mihawkgen_vk.hpp maps their names here.
#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan_core.h>
namespace ps5x360::mihawk_dispatch {
inline PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
inline PFN_vkAllocateMemory vkAllocateMemory = nullptr;
inline PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
inline PFN_vkBindImageMemory vkBindImageMemory = nullptr;
inline PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
inline PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
inline PFN_vkCmdClearColorImage vkCmdClearColorImage = nullptr;
inline PFN_vkCmdDispatch vkCmdDispatch = nullptr;
inline PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
inline PFN_vkCmdPushConstants vkCmdPushConstants = nullptr;
inline PFN_vkCmdResetQueryPool vkCmdResetQueryPool = nullptr;
inline PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = nullptr;
inline PFN_vkCreateBuffer vkCreateBuffer = nullptr;
inline PFN_vkCreateComputePipelines vkCreateComputePipelines = nullptr;
inline PFN_vkCreateDescriptorPool vkCreateDescriptorPool = nullptr;
inline PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
inline PFN_vkCreateImage vkCreateImage = nullptr;
inline PFN_vkCreateImageView vkCreateImageView = nullptr;
inline PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
inline PFN_vkCreateQueryPool vkCreateQueryPool = nullptr;
inline PFN_vkCreateSampler vkCreateSampler = nullptr;
inline PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
inline PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
inline PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
inline PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
inline PFN_vkDestroyImage vkDestroyImage = nullptr;
inline PFN_vkDestroyImageView vkDestroyImageView = nullptr;
inline PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
inline PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
inline PFN_vkDestroyQueryPool vkDestroyQueryPool = nullptr;
inline PFN_vkDestroySampler vkDestroySampler = nullptr;
inline PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
inline PFN_vkFreeMemory vkFreeMemory = nullptr;
inline PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
inline PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
inline PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
inline PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties = nullptr;
inline PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
inline PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
inline PFN_vkGetQueryPoolResults vkGetQueryPoolResults = nullptr;
inline PFN_vkMapMemory vkMapMemory = nullptr;
inline PFN_vkUnmapMemory vkUnmapMemory = nullptr;
inline PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
// Binds every function above: false (and the first missing name in `missing`)
// when one is not there.
bool Bind(VkInstance instance, VkDevice device, PFN_vkGetInstanceProcAddr get_instance_proc,
          const char** missing);
}  // namespace ps5x360::mihawk_dispatch
