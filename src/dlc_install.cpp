// SPDX-License-Identifier: MIT
#include "xbox360ps5/dlc_install.hpp"
#include "xenia/kernel/xam/content_manager.h"
#include "xenia/vfs/devices/stfs_xbox.h"
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#ifdef __linux__
#include <fcntl.h>
#include <sys/syscall.h>
#endif
#endif

namespace xbox360ps5 {
namespace {
namespace fs = std::filesystem;
using namespace xe::kernel::xam;
// PS5X360E: Xenia Edge keeps the container structures in xe::vfs.
using xe::vfs::XContentContainerHeader;
using xe::vfs::XContentVolumeType;
constexpr size_t kChunk = 64 * 1024;
constexpr size_t kMaxVisited = 4096, kMaxPackages = 256, kMaxRoots = 64;
constexpr int kMaxDepth = 4;
constexpr uint32_t kEnd = 0xFFFFFF;
// Bounds both the preflight and allocations made by Xenia's directory parser.
constexpr uint32_t kMaxTableBlocks = 256, kMaxDataBlocks = 1048576;

struct FileCloser { void operator()(FILE* f) const { if (f) fclose(f); } };
using File = std::unique_ptr<FILE, FileCloser>;
File Open(const fs::path& p, const char* mode) {
  return File(xe::filesystem::OpenFile(p, mode));
}
bool ReadAt(FILE* f, uint64_t offset, void* out, size_t bytes) {
#ifdef _WIN32
  if (_fseeki64(f, static_cast<int64_t>(offset), SEEK_SET)) return false;
#else
  if (fseeko(f, static_cast<off_t>(offset), SEEK_SET)) return false;
#endif
  return fread(out, 1, bytes, f) == bytes;
}
struct Stamp {
  uint64_t bytes = 0;
  fs::file_time_type time{};
  bool operator==(const Stamp& o) const { return bytes == o.bytes && time == o.time; }
};
bool Snapshot(const fs::path& p, Stamp& s) {
  std::error_code ec;
  if (!fs::is_regular_file(fs::symlink_status(p, ec)) || ec) return false;
  s.bytes = fs::file_size(p, ec);
  if (ec) return false;
  s.time = fs::last_write_time(p, ec);
  return !ec;
}
bool SafeName(const std::string& name) {
  if (name.empty() || name.size() > 42 || name == "." || name == ".." ||
      name.front() == '.' || name.back() == ' ' || name.back() == '.') return false;
  for (unsigned char c : name) {
    if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' ||
        c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
  }
  return true;
}
// Reject symlinked ancestors, including the saves root. No throwing fs overloads.
bool PlainPath(const fs::path& p) {
  fs::path part;
  std::error_code ec;
  for (const auto& component : p) {
    if (component == "..") return false;
    part /= component;
    const auto st = fs::symlink_status(part, ec);
    if (ec == std::errc::no_such_file_or_directory) { ec.clear(); continue; }
    if (ec || fs::is_symlink(st)) return false;
  }
  return true;
}
std::string Hex(uint32_t value) {
  char text[9];
  snprintf(text, sizeof(text), "%08X", value);
  return text;
}

// Xenia's STFS reader assumes parent indices, chain lengths and filenames are
// valid and asserts on bad chains. Check those assumptions using its actual
// on-disk structures before invoking the production package/device parser.
bool Structure(FILE* f, uint64_t size, const XContentContainerHeader& h) {
  const auto& d = h.content_metadata.volume_descriptor.stfs;
  const uint64_t start = (uint64_t(h.content_header.header_size.get()) + 4095) & ~uint64_t(4095);
  if (!d.is_valid() || !d.flags.bits.read_only_format ||
      !d.file_table_block_count || d.file_table_block_count > kMaxTableBlocks ||
      !d.total_block_count || d.total_block_count > kMaxDataBlocks ||
      start < sizeof(h) || start > size) return false;
  auto data_offset = [](uint32_t block) {
    uint64_t n = block;
    for (uint32_t level : {170u, 28900u, 4913000u}) {
      n += (uint64_t(block) + level) / level;
      if (block < level) break;
    }
    return n * 4096;
  };
  auto next = [&](uint32_t block, uint32_t& result) {
    if (block >= d.total_block_count) return false;
    // Read-only STFS uses one backing block; this is Xenia's level-0
    // BlockToHashBlockNumber calculation (the same path GetBlockHash uses).
    uint64_t hash = 0;
    if (block >= 170) {
      hash = uint64_t(block / 170) * 171 + block / 28900 + 1;
      if (block >= 28900) ++hash;
    }
    const uint64_t offset = start + hash * 4096 + (block % 170) * sizeof(xe::vfs::StfsHashEntry);
    xe::vfs::StfsHashEntry entry{};
    if (offset > size || sizeof(entry) > size - offset ||
        !ReadAt(f, offset, &entry, sizeof(entry))) return false;
    result = entry.level0_next_block();
    return true;
  };
  std::set<uint32_t> used;
  auto claim = [&](uint32_t block) {
    if (block >= d.total_block_count || !used.insert(block).second) return false;
    const uint64_t offset = start + data_offset(block);
    return offset <= size && 4096 <= size - offset;
  };
  std::vector<bool> directories;
  uint32_t table = d.file_table_block_number();
  // Claim all directory blocks first so files cannot refer to a directory block.
  std::vector<uint32_t> tables;
  for (uint32_t i = 0; i < d.file_table_block_count; ++i) {
    if (!claim(table)) return false;
    tables.push_back(table);
    if (!next(table, table)) return false;
    if (i + 1 == d.file_table_block_count ? table != kEnd : table == kEnd) return false;
  }
  for (uint32_t block : tables) {
    xe::vfs::StfsDirectoryBlock listing{};
    if (!ReadAt(f, start + data_offset(block), &listing, sizeof(listing))) return false;
    for (const auto& e : listing.entries) {
      if (!e.name[0]) break;
      if (!e.flags.name_length || e.flags.name_length > sizeof(e.name) ||
          (e.directory_index != 0xFFFF &&
           (e.directory_index >= directories.size() || !directories[e.directory_index]))) return false;
      directories.push_back(e.flags.directory);
      if (e.flags.directory) continue;
      const uint32_t count = (uint64_t(e.length.get()) + 4095) / 4096;
      if (count != e.allocated_data_blocks() || count > kMaxDataBlocks) return false;
      uint32_t data = e.start_block_number();
      for (uint32_t i = 0; i < count; ++i) {
        if (!claim(data) || !next(data, data)) return false;
        if (i + 1 == count ? data != kEnd : data == kEnd) return false;
      }
    }
  }
  return true;
}

struct PackageInfo { Stamp stamp; std::string file_name, display_name; };
bool ValidateUnchecked(ContentManager* manager, const fs::path& p, uint32_t title,
              PackageInfo& info) {
  if (!manager || !title || title == kCurrentlyRunningTitleId || !PlainPath(p) ||
      !Snapshot(p, info.stamp)) return false;
  info.file_name = xe::path_to_utf8(p.filename());
  if (!SafeName(info.file_name)) return false;
  auto f = Open(p, "rb");
  XContentContainerHeader header{};
  if (!f || info.stamp.bytes < sizeof(header) ||
      !ReadAt(f.get(), 0, &header, sizeof(header)) ||
      !header.content_header.is_magic_valid() ||
      header.content_metadata.volume_type != XContentVolumeType::kStfs ||
      header.content_metadata.content_type != xe::XContentType::kMarketplaceContent ||
      header.content_metadata.execution_info.title_id != title ||
      !header.is_package_readonly() || !Structure(f.get(), info.stamp.bytes, header)) return false;
  // Serialize with core content operations.
  // PS5X360E: Xenia Edge mounts a package through its container device (the
  // same parser the content manager uses); the header was checked above.
  auto core_lock = xe::global_critical_region::AcquireDirect();
  auto device = xe::vfs::XContentContainerDevice::CreateContentDevice(
      "\\Device\\PS5X360E_DLC_CHECK", p);
  if (!device || !device->Initialize()) return false;
  const auto metadata =
      xe::vfs::XContentContainerDevice::ContentDataFromHeader(header);
  if (metadata.title_id != title ||
      metadata.content_type != xe::XContentType::kMarketplaceContent)
    return false;
  info.display_name = xe::to_utf8(metadata.display_name());
  if (info.display_name.empty()) info.display_name = info.file_name;
  Stamp after;
  return Snapshot(p, after) && after == info.stamp;
}
bool Validate(ContentManager* manager, const fs::path& p, uint32_t title,
              PackageInfo& info) {
#if defined(__cpp_exceptions)
  // The upstream parser still has throwing file_size/is_directory calls.
  // Translate races such as a source removed after preflight into rejection.
  try { return ValidateUnchecked(manager, p, title, info); }
  catch (...) { return false; }
#else
  return ValidateUnchecked(manager, p, title, info);
#endif
}
bool Publish(const fs::path& staged, const fs::path& destination) {
#ifdef _WIN32
  return MoveFileExW(staged.c_str(), destination.c_str(), 0) != 0;
#elif defined(__linux__)
  // Do not fall back to rename(), which silently replaces an existing file.
  return syscall(SYS_renameat2, AT_FDCWD, staged.c_str(), AT_FDCWD,
                 destination.c_str(), 1 /* RENAME_NOREPLACE */) == 0;
#else
  // PS5's SDK has no rename-with-exclusion. link atomically publishes the same
  // complete inode and refuses any existing destination, then cleanup unlinks
  // staging. Unsupported hard links fail closed rather than risk replacement.
  return link(staged.c_str(), destination.c_str()) == 0;
#endif
}
}  // namespace

struct DlcInstaller::Impl {
  ContentManager* manager;
  fs::path root;
  mutable std::mutex status_mutex;
  std::mutex api_mutex;
  DlcStatus status;
  std::atomic<bool> stop{false};
  std::thread worker;
  Impl(ContentManager* m, fs::path r) : manager(m), root(std::move(r)) {}
  ~Impl() { stop.store(true); if (worker.joinable()) worker.join(); }
  void Message(const char* text, bool busy = false) {
    std::lock_guard<std::mutex> lock(status_mutex);
    status.message = text; status.busy = busy;
  }
  fs::path Destination(uint32_t title, const std::string& name) const {
    return root / "0000000000000000" / Hex(title) / "00000002" / name;
  }
  void Copy(uint32_t title, fs::path source, PackageInfo expected) {
    std::error_code ec;
    fs::path staging;
    // Only remove the private directory created by this job, never stale jobs.
    struct Cleanup {
      fs::path* path;
      ~Cleanup() { if (!path->empty()) { std::error_code e; fs::remove_all(*path, e); } }
    } cleanup{&staging};
    const auto dest = Destination(title, expected.file_name);
    const auto temp_root = root / ".dlc-install-tmp";
    if (root.empty() || !PlainPath(dest) || !PlainPath(temp_root)) {
      Message("Unsafe content path."); return;
    }
    fs::create_directories(temp_root, ec);
    if (ec) { Message("Cannot create staging directory."); return; }
    static std::atomic<uint64_t> sequence{0};
    for (int i = 0; i < 32; ++i) {
      const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
      auto candidate = temp_root / (std::to_string(token) + "-" + std::to_string(sequence.fetch_add(1)));
      if (fs::create_directory(candidate, ec)) { staging = candidate; break; }
      if (ec && ec != std::errc::file_exists) break;
      ec.clear();
    }
    if (staging.empty()) { Message("Cannot create staging directory."); return; }
    const auto archive = staging / expected.file_name;
    auto in = Open(source, "rb");
    auto out = Open(archive, "wb");
    Stamp before;
    if (!in || !out || !Snapshot(source, before) || !(before == expected.stamp)) {
      Message("Source package changed or cannot be read."); return;
    }
    std::array<uint8_t, kChunk> buffer{}, comparison{};
    uint64_t done = 0;
    while (done < expected.stamp.bytes) {
      if (stop.load()) { Message("Installation interrupted."); return; }
      const size_t count = static_cast<size_t>(std::min<uint64_t>(kChunk, expected.stamp.bytes - done));
      if (fread(buffer.data(), 1, count, in.get()) != count ||
          fwrite(buffer.data(), 1, count, out.get()) != count) {
        Message("Package copy failed."); return;
      }
      done += count;
      { std::lock_guard<std::mutex> lock(status_mutex); status.done = done; }
    }
    if (fgetc(in.get()) != EOF || ferror(in.get()) || fflush(out.get())) {
      Message("Package copy failed."); return;
    }
#ifdef _WIN32
    if (_commit(_fileno(out.get()))) { Message("Package copy failed."); return; }
#else
    if (fsync(fileno(out.get()))) { Message("Package copy failed."); return; }
#endif
    if (fclose(out.release())) { Message("Package copy failed."); return; }
    in.reset();
    Message("Verifying copied package.", true);
    auto original = Open(source, "rb"), copied = Open(archive, "rb");
    if (!original || !copied) { Message("Source package changed or cannot be read."); return; }
    // Byte comparison detects same-size edits as well as metadata changes.
    for (uint64_t offset = 0; offset < expected.stamp.bytes;) {
      if (stop.load()) { Message("Installation interrupted."); return; }
      const size_t count = static_cast<size_t>(std::min<uint64_t>(kChunk, expected.stamp.bytes - offset));
      if (fread(buffer.data(), 1, count, original.get()) != count ||
          fread(comparison.data(), 1, count, copied.get()) != count ||
          memcmp(buffer.data(), comparison.data(), count)) {
        Message("Source package changed or cannot be read."); return;
      }
      offset += count;
    }
    Stamp after;
    if (fgetc(original.get()) != EOF || ferror(original.get()) ||
        !Snapshot(source, after) || !(after == expected.stamp)) {
      Message("Source package changed or cannot be read."); return;
    }
    original.reset(); copied.reset();
    PackageInfo checked;
    if (!Validate(manager, archive, title, checked) || checked.stamp.bytes != expected.stamp.bytes) {
      Message("Copied package validation failed."); return;
    }
    if (stop.load()) { Message("Installation interrupted."); return; }
    fs::create_directories(dest.parent_path(), ec);
    if (ec) { Message("Cannot create content directory."); return; }
    if (!PlainPath(dest) || !Publish(archive, dest)) {
      Message("Destination exists or cannot be published."); return;
    }
    // Validate the installed archive too, using ContentManager, without mounting
    // into the guest VFS and without changing licenses or core configuration.
    if (!Validate(manager, dest, title, checked)) {
      // It was fully validated in staging; do not delete a published path that
      // an external writer might have replaced. Report the failure explicitly.
      Message("Installed package validation failed."); return;
    }
    Message("DLC installed.");
  }
};

DlcInstaller::DlcInstaller(ContentManager* manager, fs::path content_root)
    : impl_(std::make_unique<Impl>(manager, std::move(content_root))) {}
DlcInstaller::~DlcInstaller() = default;
DlcStatus DlcInstaller::Status() const {
  std::lock_guard<std::mutex> lock(impl_->status_mutex);
  return impl_->status;
}
std::vector<DlcEntry> DlcInstaller::Scan(uint32_t title_id, const std::vector<fs::path>& roots) {
  std::lock_guard<std::mutex> api(impl_->api_mutex);
  if (Status().busy) return {};
  if (impl_->worker.joinable()) impl_->worker.join();
  std::vector<DlcEntry> result;
  if (!impl_->manager || !title_id || title_id == kCurrentlyRunningTitleId) {
    impl_->Message("DLC installer is unavailable."); return result;
  }
  std::vector<fs::path> locations{fs::path("/app0/dlc")};
  for (size_t i = 0; i < std::min(roots.size(), kMaxRoots); ++i) {
    if (!roots[i].empty()) locations.push_back(roots[i].filename() == "dlc" ? roots[i] : roots[i] / "dlc");
  }
  std::set<fs::path> seen;
  size_t visited = 0;
  bool limited = roots.size() > kMaxRoots, failed = false;
  for (const auto& location : locations) {
    if (visited >= kMaxVisited || result.size() >= kMaxPackages) { limited = true; break; }
    if (!PlainPath(location)) continue;
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(location, ec);
    if (ec || !seen.insert(canonical).second) continue;
    fs::recursive_directory_iterator it(location, fs::directory_options::none, ec), end;
    if (ec) { if (ec != std::errc::no_such_file_or_directory) failed = true; continue; }
    while (it != end && visited < kMaxVisited && result.size() < kMaxPackages) {
      ++visited;
      const auto p = it->path();
      const auto type = it->symlink_status(ec);
      if (ec) { failed = true; break; }
      const auto name = xe::path_to_utf8(p.filename());
      if (fs::is_symlink(type) || (!name.empty() && name.front() == '.') || it.depth() >= kMaxDepth) {
        it.disable_recursion_pending();
      } else if (fs::is_regular_file(type)) {
        const auto key = fs::weakly_canonical(p, ec);
        if (!ec && seen.insert(key).second) {
          PackageInfo info;
          if (Validate(impl_->manager, p, title_id, info)) {
            PackageInfo installed;
            const bool exists = Validate(impl_->manager, impl_->Destination(title_id, info.file_name), title_id, installed);
            result.push_back({info.display_name, p, info.stamp.bytes, exists});
          }
        }
      }
      it.increment(ec);
      if (ec) { failed = true; break; }
    }
    if (it != end) limited = true;
  }
  std::sort(result.begin(), result.end(), [](const DlcEntry& a, const DlcEntry& b) {
    return a.name == b.name ? a.path < b.path : a.name < b.name;
  });
  impl_->Message(limited ? "DLC scan limit reached." : failed ? "Some DLC folders could not be read." :
                 result.empty() ? "No matching DLC packages found." : "DLC scan complete.");
  return result;
}
bool DlcInstaller::Install(uint32_t title_id, const DlcEntry& entry) {
  std::lock_guard<std::mutex> api(impl_->api_mutex);
  if (Status().busy) return false;
  if (impl_->worker.joinable()) impl_->worker.join();
  PackageInfo info;
  if (!Validate(impl_->manager, entry.path, title_id, info)) {
    impl_->Message("Invalid DLC package or title mismatch."); return false;
  }
  if (impl_->root.empty() || !PlainPath(impl_->Destination(title_id, info.file_name))) {
    impl_->Message("Unsafe content path."); return false;
  }
  std::error_code ec;
  const auto st = fs::symlink_status(impl_->Destination(title_id, info.file_name), ec);
  if (ec && ec != std::errc::no_such_file_or_directory) {
    impl_->Message("Destination exists or cannot be published."); return false;
  }
  if (fs::exists(st)) { impl_->Message("Destination exists or cannot be published."); return false; }
  { std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->status = {true, 0, info.stamp.bytes, "Installing DLC."}; }
  impl_->stop.store(false);
#if defined(__cpp_exceptions)
  try {
#endif
    impl_->worker = std::thread([impl = impl_.get(), title_id, path = entry.path, info] {
#if defined(__cpp_exceptions)
      try { impl->Copy(title_id, path, info); }
      catch (...) { impl->Message("Package copy failed."); }
#else
      impl->Copy(title_id, path, info);
#endif
    });
#if defined(__cpp_exceptions)
  } catch (...) { impl_->Message("Cannot start DLC installation."); return false; }
#endif
  return true;
}
}  // namespace xbox360ps5
