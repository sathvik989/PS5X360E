// SPDX-License-Identifier: GPL-3.0-or-later
// PS5X360E (alpha.92): builds MihawkGen's fg_common.cpp (third_party/mihawkgen,
// unchanged) with its Vulkan calls going through the functions bound at run
// time (mihawkgen_vk.hpp).
#define VK_NO_PROTOTYPES
#include "mihawkgen_vk.hpp"
#include "../../third_party/mihawkgen/framegen/src/fg_common.cpp"
