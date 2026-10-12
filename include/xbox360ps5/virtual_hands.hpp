// SPDX-License-Identifier: MIT
// Estimated hand reach from phone joints, NOT real Kinect SDK hand/image data.
// No guest ABI, direction bits, controller input, identity or temporal state.
#pragma once
#include "xbox360ps5/motion_input.hpp"
#include <cmath>

namespace xbox360ps5::motion {
struct HandVector {
  float x = 0, y = 0, z = 0;
};
struct EstimatedHand {
  HandVector vector{};  // Unit reach in native sensor axes; zero if unreliable.
  float magnitude = 0;  // Actual estimated shoulder-to-hand reach in metres.
  bool reliable = false;
  // Targeted estimated UI cursor, not original SDK image/basis projection.
  // Unavailable is the title's off-screen sentinel, never a centre hover.
  float cursor_x = 0, cursor_y = 1.125f;
};
struct TwoHands {
  EstimatedHand left{}, right{};
};

namespace estimated_hands_detail {
inline bool Valid(const Joint& joint) {
  return std::isfinite(joint.x) && std::isfinite(joint.y) &&
         std::isfinite(joint.z) && std::abs(joint.x) <= 4.0f &&
         std::abs(joint.y) <= 4.0f && std::abs(joint.z) <= 4.0f &&
         std::isfinite(joint.confidence) && joint.confidence >= 0.5f &&
         joint.confidence <= 1.0f;
}
inline EstimatedHand Reach(const Joint& hand, const Joint& wrist,
                           const Joint& shoulder, const Joint& hip) {
  if (!Valid(hand) || !Valid(wrist) || !Valid(shoulder) || !Valid(hip) ||
      hand.y <= hip.y + 0.10f) return {};
  // SkeletonSpace axes: X=-phone.X, Y=phone.Y, Z=-phone.Z. Translation
  // cancels for relative reach. Do not clamp absolute sensor distance here:
  // that would distort the actual metre reach and torso-offset invariance.
  const HandVector reach{shoulder.x - hand.x, hand.y - shoulder.y,
                         shoulder.z - hand.z};
  const float squared = reach.x * reach.x + reach.y * reach.y + reach.z * reach.z;
  if (!(squared > 0.0f) || squared > 1.5f * 1.5f) return {};
  const float magnitude = std::sqrt(squared);
  const auto limit = [](float value) { return std::fmax(-1.5f, std::fmin(1.5f, value)); };
  return {{reach.x / magnitude, reach.y / magnitude, reach.z / magnitude},
          magnitude, true, limit((hip.x - hand.x) / 0.8f),
          limit((shoulder.y - hand.y) / 0.6f)};
}
}  // namespace estimated_hands_detail

// Caller supplies Snapshot() (or equivalently age-validated input); this pure
// function does not consult clocks. Caller owns title/hash guards and the ABI.
// Each invalid/lost input yields fresh zero output, never a retained command.
inline TwoHands EstimateHands(const Frame& pose) {
  if (!pose.tracked) return {};
  return {estimated_hands_detail::Reach(pose.joints[7], pose.joints[6],
                                       pose.joints[4], pose.joints[0]),
          estimated_hands_detail::Reach(pose.joints[11], pose.joints[10],
                                       pose.joints[8], pose.joints[0])};
}
}  // namespace xbox360ps5::motion
