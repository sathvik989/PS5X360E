// SPDX-License-Identifier: GPL-3.0-or-later
// PS5X360E (alpha.92): the vk* names MihawkGen's sources call, mapped to the
// functions bound at run time (mihawkgen_dispatch.hpp). Included before them,
// in their own translation units only (mihawkgen_lib_*.cpp).
#pragma once
#include "mihawkgen_dispatch.hpp"
#define vkAllocateDescriptorSets ps5x360::mihawk_dispatch::vkAllocateDescriptorSets
#define vkAllocateMemory ps5x360::mihawk_dispatch::vkAllocateMemory
#define vkBindBufferMemory ps5x360::mihawk_dispatch::vkBindBufferMemory
#define vkBindImageMemory ps5x360::mihawk_dispatch::vkBindImageMemory
#define vkCmdBindDescriptorSets ps5x360::mihawk_dispatch::vkCmdBindDescriptorSets
#define vkCmdBindPipeline ps5x360::mihawk_dispatch::vkCmdBindPipeline
#define vkCmdClearColorImage ps5x360::mihawk_dispatch::vkCmdClearColorImage
#define vkCmdDispatch ps5x360::mihawk_dispatch::vkCmdDispatch
#define vkCmdPipelineBarrier ps5x360::mihawk_dispatch::vkCmdPipelineBarrier
#define vkCmdPushConstants ps5x360::mihawk_dispatch::vkCmdPushConstants
#define vkCmdResetQueryPool ps5x360::mihawk_dispatch::vkCmdResetQueryPool
#define vkCmdWriteTimestamp ps5x360::mihawk_dispatch::vkCmdWriteTimestamp
#define vkCreateBuffer ps5x360::mihawk_dispatch::vkCreateBuffer
#define vkCreateComputePipelines ps5x360::mihawk_dispatch::vkCreateComputePipelines
#define vkCreateDescriptorPool ps5x360::mihawk_dispatch::vkCreateDescriptorPool
#define vkCreateDescriptorSetLayout ps5x360::mihawk_dispatch::vkCreateDescriptorSetLayout
#define vkCreateImage ps5x360::mihawk_dispatch::vkCreateImage
#define vkCreateImageView ps5x360::mihawk_dispatch::vkCreateImageView
#define vkCreatePipelineLayout ps5x360::mihawk_dispatch::vkCreatePipelineLayout
#define vkCreateQueryPool ps5x360::mihawk_dispatch::vkCreateQueryPool
#define vkCreateSampler ps5x360::mihawk_dispatch::vkCreateSampler
#define vkCreateShaderModule ps5x360::mihawk_dispatch::vkCreateShaderModule
#define vkDestroyBuffer ps5x360::mihawk_dispatch::vkDestroyBuffer
#define vkDestroyDescriptorPool ps5x360::mihawk_dispatch::vkDestroyDescriptorPool
#define vkDestroyDescriptorSetLayout ps5x360::mihawk_dispatch::vkDestroyDescriptorSetLayout
#define vkDestroyImage ps5x360::mihawk_dispatch::vkDestroyImage
#define vkDestroyImageView ps5x360::mihawk_dispatch::vkDestroyImageView
#define vkDestroyPipeline ps5x360::mihawk_dispatch::vkDestroyPipeline
#define vkDestroyPipelineLayout ps5x360::mihawk_dispatch::vkDestroyPipelineLayout
#define vkDestroyQueryPool ps5x360::mihawk_dispatch::vkDestroyQueryPool
#define vkDestroySampler ps5x360::mihawk_dispatch::vkDestroySampler
#define vkDestroyShaderModule ps5x360::mihawk_dispatch::vkDestroyShaderModule
#define vkFreeMemory ps5x360::mihawk_dispatch::vkFreeMemory
#define vkGetBufferMemoryRequirements ps5x360::mihawk_dispatch::vkGetBufferMemoryRequirements
#define vkGetDeviceProcAddr ps5x360::mihawk_dispatch::vkGetDeviceProcAddr
#define vkGetImageMemoryRequirements ps5x360::mihawk_dispatch::vkGetImageMemoryRequirements
#define vkGetPhysicalDeviceFormatProperties ps5x360::mihawk_dispatch::vkGetPhysicalDeviceFormatProperties
#define vkGetPhysicalDeviceMemoryProperties ps5x360::mihawk_dispatch::vkGetPhysicalDeviceMemoryProperties
#define vkGetPhysicalDeviceProperties ps5x360::mihawk_dispatch::vkGetPhysicalDeviceProperties
#define vkGetQueryPoolResults ps5x360::mihawk_dispatch::vkGetQueryPoolResults
#define vkMapMemory ps5x360::mihawk_dispatch::vkMapMemory
#define vkUnmapMemory ps5x360::mihawk_dispatch::vkUnmapMemory
#define vkUpdateDescriptorSets ps5x360::mihawk_dispatch::vkUpdateDescriptorSets
