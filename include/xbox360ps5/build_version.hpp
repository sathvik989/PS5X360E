// SPDX-License-Identifier: MIT
#pragma once
namespace xbox360ps5 {
#ifdef XBOX360PS5_VERSION
inline constexpr char kBuildVersionLabel[] = "PS5X360E v" XBOX360PS5_VERSION;
#else
inline constexpr char kBuildVersionLabel[] = "PS5X360E development build";
#endif
}
