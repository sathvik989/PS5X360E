// SPDX-License-Identifier: MIT
// The game sheet's DLC page. Every package header is read here directly (magic, content type,
// title ID, English name), so the page lists what a game has and says why a file is not used,
// instead of dropping it silently.
//   active:    <saves>/0000000000000000/<TITLE>/00000002/<file>      (the folder the game lists)
//   disabled:  <saves>/0000000000000000/<TITLE>/00000002-off/<file>  (kept, not listed)
//   available: a dlc folder (PPSA50360/dlc, the game's dlc folder, each library root's dlc)
// Enabling and disabling move the file between the first two; installing copies an available
// package into the first (staged in 00000002-installing, checked, then renamed).
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xbox360ps5::dlc2 {
namespace fs = std::filesystem;

enum class State { active, disabled, available };
struct Item {
  std::string name, file;
  fs::path path;
  uint64_t bytes = 0;
  State state = State::available;
};
struct Found {
  std::vector<Item> items;
  int other_game = 0, not_dlc = 0, not_package = 0;  // files in dlc folders that are not used
};

inline uint32_t Be32(const unsigned char* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
struct Header {
  bool package = false;
  uint32_t type = 0, title = 0;
  std::string name;
};
// Offsets of the Xbox 360 content header: magic 0x000, content type 0x344, title ID 0x360,
// English display name 0x411 (UTF-16BE, 128 characters).
inline Header Read(const fs::path& path) {
  Header h;
  std::ifstream in(path, std::ios::binary);
  unsigned char b[0x511] = {};
  if (!in.read(reinterpret_cast<char*>(b), sizeof(b))) return h;
  const std::string magic(reinterpret_cast<char*>(b), 4);
  if (magic != "LIVE" && magic != "PIRS" && magic != "CON ") return h;
  h.package = true;
  h.type = Be32(b + 0x344);
  h.title = Be32(b + 0x360);
  for (int i = 0; i < 128; ++i) {
    const unsigned c = unsigned(b[0x411 + 2 * i]) << 8 | b[0x412 + 2 * i];
    if (!c) break;
    if (c < 0x80) {
      h.name += char(c);
    } else if (c < 0x800) {
      h.name += char(0xC0 | (c >> 6));
      h.name += char(0x80 | (c & 0x3F));
    } else {
      h.name += char(0xE0 | (c >> 12));
      h.name += char(0x80 | ((c >> 6) & 0x3F));
      h.name += char(0x80 | (c & 0x3F));
    }
  }
  return h;
}

inline std::string Hex8(uint32_t value) {
  char text[9];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}
inline fs::path Folder(const fs::path& root, uint32_t title, const char* leaf) {
  return root / "0000000000000000" / Hex8(title) / leaf;
}
inline fs::path ActiveFolder(const fs::path& root, uint32_t title) { return Folder(root, title, "00000002"); }
inline fs::path DisabledFolder(const fs::path& root, uint32_t title) { return Folder(root, title, "00000002-off"); }

inline Found Find(uint32_t title, const fs::path& root, const std::vector<fs::path>& inboxes) {
  Found out;
  std::map<std::string, bool> listed;
  const auto add = [&](const fs::path& p, State state) {
    std::error_code ec;
    const uint64_t size = fs::file_size(p, ec);
    if (ec) return;
    const Header h = Read(p);
    const bool inbox = state == State::available;
    if (!h.package) { if (inbox) ++out.not_package; return; }
    if (h.title != title) { if (inbox) ++out.other_game; return; }
    if (h.type != 0x00000002) { if (inbox) ++out.not_dlc; return; }
    const std::string file = p.filename().string();
    if (listed.count(file)) return;  // the active or disabled copy wins over a dlc-folder copy
    listed[file] = true;
    out.items.push_back({h.name.empty() ? file : h.name, file, p, size, state});
  };
  if (!root.empty() && title) {
    for (const auto& [folder, state] : {std::pair{ActiveFolder(root, title), State::active},
                                        std::pair{DisabledFolder(root, title), State::disabled}}) {
      std::error_code ec;
      for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) add(it->path(), state);
    }
  }
  for (const auto& inbox : inboxes) {
    std::error_code ec;
    size_t visited = 0;
    fs::recursive_directory_iterator it(inbox, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && visited < 4096; it.increment(ec), ++visited) {
      const std::string name = it->path().filename().string();
      const bool hidden = !name.empty() && name[0] == '.', link = it->is_symlink(ec);
      const bool directory = !link && it->is_directory(ec);
      if (hidden || link) { if (directory || link) it.disable_recursion_pending(); continue; }
      if (directory) { if (it.depth() >= 3) it.disable_recursion_pending(); continue; }
      if (it->is_regular_file(ec)) add(it->path(), State::available);
    }
  }
  std::stable_sort(out.items.begin(), out.items.end(), [](const Item& a, const Item& b) {
    return a.state != b.state ? int(a.state) < int(b.state) : a.name < b.name;
  });
  return out;
}

// Enable (to the active folder) or disable (to the disabled folder): a rename, nothing copied.
inline bool Move(const Item& item, const fs::path& root, uint32_t title, bool enable) {
  const fs::path folder = enable ? ActiveFolder(root, title) : DisabledFolder(root, title);
  std::error_code ec;
  fs::create_directories(folder, ec);
  const fs::path target = folder / item.file;
  if (fs::exists(target, ec)) return false;
  fs::rename(item.path, target, ec);
  return !ec;
}

// Installing copies on a worker thread, with progress.
class Copier {
 public:
  ~Copier() { if (worker_.joinable()) worker_.detach(); }
  bool Busy() const { return busy_.load(); }
  double Progress() const { return total_ ? double(done_.load()) / double(total_) : 0.0; }
  std::string Name() { std::lock_guard lock(mutex_); return name_; }
  // 0 while copying or never run; 1 installed; negative: failed (see Result).
  int TakeResult() { const int r = result_.exchange(0); return r; }
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
    const fs::path active = ActiveFolder(root, title), staging = Folder(root, title, "00000002-installing");
    std::error_code ec;
    fs::create_directories(active, ec);
    fs::create_directories(staging, ec);
    const fs::path target = active / item.file, temporary = staging / item.file;
    if (fs::exists(target, ec)) return -1;
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
    if (!h.package || h.title != title || h.type != 0x00000002) { fs::remove(temporary, ec); return -4; }
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
}  // namespace xbox360ps5::dlc2
