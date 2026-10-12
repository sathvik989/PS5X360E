// SPDX-License-Identifier: MIT
// The game sheet's Updates page: title updates, Xbox 360 content type 000B0000.
//   active:    <saves>/0000000000000000/<TITLE>/000B0000/<file>      (the emulator applies it at launch)
//   disabled:  <saves>/0000000000000000/<TITLE>/000B0000-off/<file>  (kept, not applied)
//   available: an updates or dlc folder (PPSA50360/updates, the game's own, each library root's)
// The emulator applies the first package it lists in the active folder, whichever that is, so
// one update is active at a time: activating one moves the others to the disabled folder.
// An update is made for one edition of a game (its media ID, kept in both headers); one made
// for another edition is listed with that reason and is not installed by itself.
#pragma once
#include "dlc_manager.hpp"

namespace xbox360ps5::tu {
namespace fs = std::filesystem;
using dlc2::Be32;
using dlc2::Folder;

constexpr uint32_t kUpdateType = 0x000B0000;
inline constexpr char kActiveLeaf[] = "000B0000", kDisabledLeaf[] = "000B0000-off",
                      kStagingLeaf[] = "000B0000-installing";

enum class State { active, disabled, available };
struct Item {
  std::string name, file;
  fs::path path;
  uint64_t bytes = 0;
  State state = State::available;
  uint32_t version = 0, media = 0;
};
struct Found {
  std::vector<Item> items;
  int other_game = 0, not_update = 0, not_package = 0;  // files in updates folders that are not used
};
// A folder to look in. Files that are not updates are counted only for an updates folder: in a
// dlc folder they are that page's business.
struct Inbox {
  fs::path folder;
  bool count_ignored = false;
};

struct Header {
  bool package = false;
  uint32_t type = 0, media = 0, version = 0, title = 0;
  std::string name;
};
// Offsets of the Xbox 360 content header: content type 0x344, media ID 0x354, version 0x358,
// title ID 0x360; the display name is read as on the DLC page.
inline Header Read(const fs::path& path) {
  Header h;
  const dlc2::Header common = dlc2::Read(path);
  if (!common.package) return h;
  std::ifstream in(path, std::ios::binary);
  unsigned char b[0x364] = {};
  if (!in.read(reinterpret_cast<char*>(b), sizeof(b))) return h;
  h.package = true;
  h.type = common.type;
  h.title = common.title;
  h.name = common.name;
  h.media = Be32(b + 0x354);
  h.version = Be32(b + 0x358);
  return h;
}

// major.minor.build.revision, as the console shows a title's version.
inline std::string VersionText(uint32_t version) {
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", version >> 28, (version >> 24) & 15, (version >> 8) & 0xFFFF,
                version & 0xFF);
  return text;
}

// The media ID of a game: from a package header, or from an executable's execution information
// (optional header 0x00040006: media ID, version, base version, title ID). 0 when the file is
// neither (a disc image), so nothing is concluded from it.
inline uint32_t GameMedia(const fs::path& game, uint32_t title) {
  std::ifstream in(game, std::ios::binary);
  unsigned char head[0x364] = {};
  in.read(reinterpret_cast<char*>(head), sizeof(head));
  const std::streamsize got = in.gcount();
  if (got < 0x18) return 0;
  const std::string magic(reinterpret_cast<char*>(head), 4);
  if (magic == "LIVE" || magic == "PIRS" || magic == "CON ")
    return got == std::streamsize(sizeof(head)) && Be32(head + 0x360) == title ? Be32(head + 0x354) : 0;
  if (magic != "XEX2") return 0;
  const uint32_t count = Be32(head + 0x14);
  if (count > 96) return 0;
  in.clear();
  for (uint32_t n = 0; n < count; ++n) {
    unsigned char entry[8];
    in.seekg(std::streamoff(0x18 + n * 8));
    if (!in.read(reinterpret_cast<char*>(entry), sizeof(entry))) return 0;
    if (Be32(entry) != 0x00040006) continue;
    unsigned char info[16];
    in.seekg(std::streamoff(Be32(entry + 4)));
    if (!in.read(reinterpret_cast<char*>(info), sizeof(info))) return 0;
    return Be32(info + 12) == title ? Be32(info) : 0;
  }
  return 0;
}
// False only when both media IDs are known and differ.
inline bool Fits(const Item& item, uint32_t game_media) {
  return !game_media || !item.media || item.media == game_media;
}

inline fs::path ActiveFolder(const fs::path& root, uint32_t title) { return Folder(root, title, kActiveLeaf); }
inline fs::path DisabledFolder(const fs::path& root, uint32_t title) { return Folder(root, title, kDisabledLeaf); }

inline Found Find(uint32_t title, const fs::path& root, const std::vector<Inbox>& inboxes) {
  Found out;
  std::map<std::string, bool> listed;
  const auto add = [&](const fs::path& p, State state, bool count_ignored) {
    std::error_code ec;
    const uint64_t size = fs::file_size(p, ec);
    if (ec) return;
    const Header h = Read(p);
    if (!h.package) { if (count_ignored) ++out.not_package; return; }
    if (h.title != title) { if (count_ignored) ++out.other_game; return; }
    if (h.type != kUpdateType) { if (count_ignored) ++out.not_update; return; }
    const std::string file = p.filename().string();
    if (listed.count(file)) return;  // the installed copy wins over a copy still in a folder
    listed[file] = true;
    out.items.push_back({h.name.empty() ? file : h.name, file, p, size, state, h.version, h.media});
  };
  if (!root.empty() && title) {
    for (const auto& [folder, state] : {std::pair{ActiveFolder(root, title), State::active},
                                        std::pair{DisabledFolder(root, title), State::disabled}}) {
      std::error_code ec;
      for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) add(it->path(), state, false);
    }
  }
  for (const auto& inbox : inboxes) {
    std::error_code ec;
    size_t visited = 0;
    fs::recursive_directory_iterator it(inbox.folder, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && visited < 4096; it.increment(ec), ++visited) {
      const std::string name = it->path().filename().string();
      const bool hidden = !name.empty() && name[0] == '.', link = it->is_symlink(ec);
      const bool directory = !link && it->is_directory(ec);
      if (hidden || link) { if (directory || link) it.disable_recursion_pending(); continue; }
      if (directory) { if (it.depth() >= 3) it.disable_recursion_pending(); continue; }
      if (it->is_regular_file(ec)) add(it->path(), State::available, inbox.count_ignored);
    }
  }
  std::stable_sort(out.items.begin(), out.items.end(), [](const Item& a, const Item& b) {
    return a.state != b.state ? int(a.state) < int(b.state) : a.version > b.version;
  });
  return out;
}

// Moves a package of the active folder to the disabled one. A copy of the same size already
// there is the same package: the active one is then simply dropped.
inline bool Disable(const fs::path& from, const fs::path& root, uint32_t title) {
  const fs::path folder = DisabledFolder(root, title), target = folder / from.filename();
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (fs::exists(target, ec)) {
    const uint64_t kept = fs::file_size(target, ec), moved = ec ? 0 : fs::file_size(from, ec);
    if (ec || kept != moved) return false;
    return fs::remove(from, ec) && !ec;
  }
  fs::rename(from, target, ec);
  return !ec;
}
// Leaves the active folder without packages, or with `keep` alone.
inline bool DisableOthers(const fs::path& root, uint32_t title, const std::string& keep = {}) {
  std::vector<fs::path> others;
  std::error_code ec;
  for (fs::directory_iterator it(ActiveFolder(root, title), ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file(ec) && it->path().filename().string() != keep) others.push_back(it->path());
  bool done = true;
  for (const auto& other : others) done = Disable(other, root, title) && done;
  return done;
}
// A disabled update becomes the active one.
inline bool Activate(const Item& item, const fs::path& root, uint32_t title) {
  const fs::path folder = ActiveFolder(root, title), target = folder / item.file;
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (fs::exists(target, ec) || !DisableOthers(root, title)) return false;
  fs::rename(item.path, target, ec);
  return !ec;
}

// What Play installs by itself: the newest update that fits the game, waiting in a folder, and
// newer than every installed one. An update the player turned off is therefore not put back.
inline const Item* Pending(const Found& found, uint32_t game_media) {
  uint32_t installed = 0;
  bool any = false;
  for (const auto& item : found.items)
    if (item.state != State::available) { installed = std::max(installed, item.version); any = true; }
  const Item* best = nullptr;
  for (const auto& item : found.items)
    if (item.state == State::available && Fits(item, game_media) && (!any || item.version > installed) &&
        (!best || item.version > best->version)) best = &item;
  return best;
}

// Installing copies on a worker thread, with progress, then makes the copy the active update.
class Copier {
 public:
  ~Copier() { if (worker_.joinable()) worker_.detach(); }
  bool Busy() const { return busy_.load(); }
  double Progress() const { return total_ ? double(done_.load()) / double(total_) : 0.0; }
  std::string Name() { std::lock_guard lock(mutex_); return name_; }
  // 0 while copying or never run; 1 installed; negative: the step that failed.
  int TakeResult() { return result_.exchange(0); }
  bool Start(const Item& item, const fs::path& root, uint32_t title) {
    if (busy_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    { std::lock_guard lock(mutex_); name_ = item.name; }
    done_ = 0;
    total_ = item.bytes;
    result_ = 0;
    worker_ = std::thread([this, item, root, title] {
      const int result = Run(item, root, title);
      result_ = result;
      busy_ = false;
    });
    return true;
  }

 private:
  int Run(const Item& item, const fs::path& root, uint32_t title) {
    const fs::path active = ActiveFolder(root, title), staging = Folder(root, title, kStagingLeaf);
    std::error_code ec;
    fs::create_directories(active, ec);
    fs::create_directories(staging, ec);
    const fs::path target = active / item.file, temporary = staging / item.file;
    {
      std::ifstream in(item.path, std::ios::binary);
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      if (!in || !out) return -2;
      std::vector<char> buffer(4u << 20);
      while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        out.write(buffer.data(), got);
        if (!out) break;
        done_ += uint64_t(got);
      }
      out.close();
      if (!out || done_.load() != total_) { fs::remove(temporary, ec); return -3; }
    }
    const Header h = Read(temporary);
    if (!h.package || h.title != title || h.type != kUpdateType) { fs::remove(temporary, ec); return -4; }
    // The update in use until now steps aside only once its replacement is whole.
    if (!DisableOthers(root, title)) { fs::remove(temporary, ec); return -6; }
    fs::rename(temporary, target, ec);
    if (ec) { fs::remove(temporary, ec); return -5; }
    return 1;
  }
  std::mutex mutex_;
  std::string name_;
  std::thread worker_;
  std::atomic<bool> busy_{false};
  std::atomic<uint64_t> done_{0};
  uint64_t total_ = 0;
  std::atomic<int> result_{0};
};
inline Copier& copier() {
  static Copier instance;
  return instance;
}
}  // namespace xbox360ps5::tu
