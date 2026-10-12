// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ps5x360_disc {
// Terminal Disc / Disk / Disco N token; structural evidence only.
unsigned DiscNumberFromName(const std::string& name);
struct ExecutionInfo {
  uint32_t media_id=0, version=0, base_version=0, title_id=0, savegame_id=0;
  uint8_t platform=0, executable_table=0, disc_number=0, disc_count=0;
};
struct MetadataResult {
  std::optional<ExecutionInfo> info;
  std::string error;
};
// Reads only the clear XEX2 optional-header area, never executes/decrypts an image.
MetadataResult ParseXexExecutionInfo(const uint8_t* prefix, size_t bytes);
MetadataResult ReadXexExecutionInfo(const std::string& default_xex);
// A mounted image adapter reads its default.xex header and calls the same parser.
// It must not remount the ACTIVE guest device as a side effect of this callback.
using Probe = std::function<MetadataResult(const std::string& mount_input)>;
struct Options {
  // Empty means the directory containing the physical disc family / playlist.
  // Canonical paths must remain inside one of these read-only media roots.
  std::vector<std::string> media_roots;
  size_t max_members=32, max_directory_entries=4096;
};
enum class Error {
  None, InvalidPath, UnsupportedMedia, MissingMedia, MalformedPlaylist,
  AmbiguousPlaylist, DuplicatePath, TooManyMembers, MissingMetadata,
  InvalidMetadata, IdentityMismatch, DiscNumberMismatch, AmbiguousDisc,
  LaunchedDiscMissing, RequestedDiscMissing
};
struct Candidate {
  std::string path; // normalized/canonical mount input: default.xex, .iso or .zar
  ExecutionInfo execution;
  size_t source_index=0; // original zero-based playlist/enumeration index
  unsigned name_disc_hint=0; // filename evidence only, never a guest disc index
  bool launched=false;
};
struct DiscSet {
  std::string source; // explicit playlist path or physical sibling family directory
  std::vector<Candidate> candidates; // sorted by XEX disc_number, then path
  std::optional<size_t> requested_index; // index into candidates, NOT disc_number
  bool complete=false; // all advertised discs are available, not a runtime claim
  Error error=Error::None;
  std::string detail;
  explicit operator bool() const {return error==Error::None && requested_index.has_value();}
};
// Strict path grammar, POSIX/Windows drive roots; refuses UNC/URI/drive-relative forms.
// Lexical result only. Discovery additionally resolves filesystem symlinks and roots.
std::string NormalizePath(const std::string& base, const std::string& entry,
                          const std::vector<std::string>& allowed_roots);
// Standalone playlist parser. No recursion. Relative paths use playlist's directory.
std::vector<std::string> ParseM3u(const std::string& text, const std::string& playlist,
                                const Options& options = {});
// Call after launch; preserves no state in GameEntry/Launcher/Emulator layouts.
// requested_disc is one-based Xbox XEX disc_number, bounded to 1..255.
// Explicit adjacent discs.m3u wins; a bad explicit playlist NEVER falls back silently.
DiscSet DiscoverDiscSet(const std::string& launched_path, unsigned requested_disc,
                       const Options& options = {}, const Probe& image_probe = {});
// Validate a freshly probed target immediately before main's VFS transaction.
// MediaID may differ across discs; if this target's MediaID changed, reject it.
bool ValidateTarget(const Candidate& selected, const ExecutionInfo& running,
                    unsigned requested_disc, const ExecutionInfo& freshly_read,
                    std::string& reason);
const char* ErrorName(Error);
}

namespace xbox360ps5::multidisc {
// Structural only: the runtime must read/validate each candidate's XEX before swapping.
// At most 16 paths, normalized/canonical and associated with the exact launched medium.
// Explicit discs.m3u takes precedence; malformed/ambiguous explicit sets fail closed.
std::vector<std::filesystem::path> CandidatesForDisc(
    const std::filesystem::path& launched, uint32_t requested, std::string& error);
}
