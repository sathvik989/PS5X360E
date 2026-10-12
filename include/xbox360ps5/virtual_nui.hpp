// SPDX-License-Identifier: MIT
// Single virtual player, associated with an existing signed-in profile slot.
#pragma once
#include "xbox360ps5/virtual_skeleton.hpp"
namespace xbox360ps5::motion {
constexpr uint32_t kVirtualNuiTitle = 0x4E4D084E;
constexpr uint32_t kVirtualEnrollment = 0;
struct NuiBinding {
  uint32_t session = 0, tracking_id = 0;
  uint32_t enrollment = kEnrollmentUnknown, user_index = kUserIndexNone;
  std::chrono::steady_clock::time_point delivered{};
};
inline std::mutex nui_mutex;
inline NuiBinding nui_binding;
inline std::atomic<uint32_t> nui_identity_callback{0};
inline std::atomic<uint64_t> nui_identity_notifications{0};

inline NuiBinding PublishNuiFrame(const Frame& pose, uint32_t tracking_id,
                                 uint32_t signed_in_mask) {
  std::lock_guard lock(nui_mutex);
  const uint32_t previous_user = nui_binding.user_index;
  nui_binding = {};
  nui_binding.delivered = std::chrono::steady_clock::now();
  nui_binding.session = pose.session;
  nui_binding.tracking_id = pose.tracked ? tracking_id : 0;
  if (!nui_binding.tracking_id) return nui_binding;
  // Preserve a real slot across frames; otherwise choose the first signed-in
  // slot. Never substitute controller zero for a missing profile.
  uint32_t slot = previous_user;
  if (slot >= 4 || !(signed_in_mask & (1u << slot))) {
    for (slot = 0; slot < 4 && !(signed_in_mask & (1u << slot)); ++slot) {}
  }
  if (slot < 4) {
    nui_binding.user_index = slot;
    nui_binding.enrollment = kVirtualEnrollment;
  }
  return nui_binding;
}
inline bool VirtualNuiCameraLive(uint32_t title_id) {
  if (title_id != kVirtualNuiTitle || !enabled.load() ||
      !guest_camera_open.load() || !guest_skeleton_bound.load()) return false;
  std::lock_guard lock(nui_mutex);
  return nui_binding.delivered != std::chrono::steady_clock::time_point{} &&
      std::chrono::steady_clock::now() - nui_binding.delivered <=
          std::chrono::milliseconds(500);
}
inline NuiBinding LiveNuiBinding(uint32_t title_id, uint32_t signed_in_mask) {
  if (!VirtualNuiCameraLive(title_id)) return {};
  const Frame pose = Snapshot();
  std::lock_guard lock(nui_mutex);
  if (!pose.tracked || pose.session != nui_binding.session ||
      !nui_binding.tracking_id || nui_binding.user_index >= 4 ||
      !(signed_in_mask & (1u << nui_binding.user_index))) return {};
  return nui_binding;
}
inline void ResetVirtualNui() {
  std::lock_guard lock(nui_mutex);
  nui_binding = {};
  nui_identity_callback = 0;
  nui_identity_notifications = 0;
}
}
