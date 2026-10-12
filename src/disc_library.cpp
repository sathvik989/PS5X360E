// SPDX-License-Identifier: MIT
#include "disc_library.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace xbox360ps5::multidisc {
namespace {
namespace fs = std::filesystem;
std::string Text(const fs::path& p) {
  auto u = p.generic_u8string();
  return {reinterpret_cast<const char*>(u.data()), u.size()};
}
std::string Lower(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}
void Require(bool condition, const char* error) {
  if (!condition) throw std::runtime_error(error);
}
uint32_t BE(const unsigned char* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
uint32_t Region(const fs::path& p) {
  // XEX2 fixed header.security_offset (0x10), security_info.region (0x178).
  // Source: xenia/kernel/util/xex2_info.h. Never read an unbounded image body.
  std::ifstream file(p, std::ios::binary);
  std::array<unsigned char, 24> fixed{};
  Require(bool(file.read(reinterpret_cast<char*>(fixed.data()), fixed.size())), "Missing XEX region header");
  const uint32_t header = BE(fixed.data() + 8), offset = BE(fixed.data() + 16);
  Require(header <= (2u << 20) && offset >= 24 && uint64_t(offset) + 0x184 <= header,
          "XEX security region out of bounds");
  file.seekg(offset);
  std::array<unsigned char, 0x184> security{};
  Require(bool(file.read(reinterpret_cast<char*>(security.data()), security.size())), "Truncated XEX security header");
  Require(BE(security.data()) >= security.size() && uint64_t(offset) + BE(security.data()) <= header,
          "Invalid XEX security header size");
  const auto region = BE(security.data() + 0x178);
  Require(region != 0, "Missing XEX region mask");
  return region;
}
fs::path DefaultXex(const fs::path& dir) {
  fs::path result;
  size_t count = 0;
  for (const auto& entry : fs::directory_iterator(dir)) {
    Require(++count <= 4096, "Disc directory entry limit exceeded");
    if (Lower(Text(entry.path().filename())) != "default.xex") continue;
    Require(result.empty() && entry.is_regular_file() && !entry.is_symlink(), "Ambiguous or linked default.xex");
    result = entry.path();
  }
  Require(!result.empty(), "Named disc directory has no default.xex");
  return result;
}
}

std::string DiscCountText(unsigned count, int language) {
  const bool latin = language == 9 || language == 5;
  return std::to_string(count) + " " + (latin ? (count == 1 ? "disco" : "discos") : (count == 1 ? "disc" : "discs"));
}

std::optional<ExtractedDiscGroup> InspectExtractedDiscGroup(const fs::path& parent, std::string& error) {
  error.clear();
  try {
    std::map<unsigned, fs::path> dirs;
    size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(parent)) {
      Require(++entries <= 4096, "Game directory entry limit exceeded");
      const unsigned number = ps5x360_disc::DiscNumberFromName(Text(entry.path().filename()));
      if (!number || !entry.is_directory()) continue;
      Require(!entry.is_symlink(), "Linked disc directories are not grouped");
      Require(number <= 16 && dirs.size() < 16, "Disc count exceeds 16");
      Require(dirs.emplace(number, entry.path()).second, "Ambiguous sibling disc number");
    }
    if (dirs.empty()) return {};
    Require(dirs.count(1), "Disc 1 is missing; individual entries retained");
    Require(dirs.size() >= 2, "Incomplete disc set; individual entries retained");
    ExtractedDiscGroup result;
    result.launch_path = DefaultXex(dirs.at(1));
    ps5x360_disc::Options options;
    options.max_members = 16;
    const auto set = ps5x360_disc::DiscoverDiscSet(Text(result.launch_path), 1, options);
    if (!set) throw std::runtime_error(set.detail);
    Require(set.complete && set.candidates.size() == dirs.size(), "Incomplete or conflicting playlist/folder set");
    const auto region = Region(result.launch_path);
    std::set<fs::path> canonical_members;
    for (const auto& candidate : set.candidates) {
      const unsigned number = candidate.execution.disc_number;
      Require(dirs.count(number), "Playlist disc does not match sibling directory numbers");
      const auto original = DefaultXex(dirs.at(number));
      Require(Text(fs::canonical(original)) == candidate.path, "Playlist points outside named sibling set");
      Require(canonical_members.insert(fs::canonical(original)).second, "Duplicate physical disc");
      Require(Region(original) == region, "Disc region masks differ; individual entries retained");
      result.directories.push_back(dirs.at(number));
    }
    result.title_id = set.candidates.front().execution.title_id;
    result.disc_count = unsigned(set.candidates.size());
    return result;
  } catch (const std::exception& failure) {
    error = failure.what();
    return {};
  }
}
}
