// SPDX-License-Identifier: MIT
#pragma once
#include "disc_catalog.hpp"

namespace xbox360ps5::multidisc {
// Xbox UI language IDs: 9 Portuguese, 5 Spanish, otherwise English.
std::string DiscCountText(unsigned count, int language);
struct ExtractedDiscGroup {
  std::filesystem::path launch_path; // Original Disc 1 path, never canonicalized for cache identity.
  std::vector<std::filesystem::path> directories; // Exact sibling paths to suppress in this scan only.
  uint32_t title_id = 0;
  unsigned disc_count = 0;
};
// Complete, metadata-validated extracted sets only; failure hides no library entries.
// An empty error means no named disc directories were present.
std::optional<ExtractedDiscGroup> InspectExtractedDiscGroup(
    const std::filesystem::path& parent, std::string& error);
}
