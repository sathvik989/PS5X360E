// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace xe::kernel::xam { class ContentManager; }

namespace xbox360ps5 {
struct DlcEntry {
  std::string name;
  std::filesystem::path path;
  uint64_t bytes = 0;
  bool installed = false;
};
struct DlcStatus {
  bool busy = false;
  uint64_t done = 0, total = 0;
  std::string message;
};

// The manager must outlive this object; content_root must be its saves root.
// Call Scan/Install from the setup UI thread. Status is safe on any thread.
class DlcInstaller {
 public:
  DlcInstaller(xe::kernel::xam::ContentManager* manager,
               std::filesystem::path content_root);
  ~DlcInstaller();
  DlcInstaller(const DlcInstaller&) = delete;
  DlcInstaller& operator=(const DlcInstaller&) = delete;
  std::vector<DlcEntry> Scan(
      uint32_t title_id, const std::vector<std::filesystem::path>& roots);
  bool Install(uint32_t title_id, const DlcEntry& entry);
  DlcStatus Status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace xbox360ps5
