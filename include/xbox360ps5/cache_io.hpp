// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace xbox360ps5 {
// A failed write leaves the last complete cache in place. No game/save data.
inline bool WriteCacheAtomically(const std::filesystem::path& path,
                                const void* bytes, size_t size) {
  static std::atomic<uint64_t> serial{0};
  const auto temporary = path.string() + ".pending-" + std::to_string(serial.fetch_add(1));
  FILE* file = std::fopen(temporary.c_str(), "wb");
  if (!file) return false;
  bool ok = !size || std::fwrite(bytes, 1, size, file) == size;
  if (std::fflush(file)) ok = false;
#ifdef _WIN32
  if (ok && _commit(_fileno(file))) ok = false;
#else
  if (ok && fsync(fileno(file))) ok = false;
#endif
  if (std::fclose(file)) ok = false;
#ifdef _WIN32
  if (ok && MoveFileExW(std::filesystem::path(temporary).c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
#else
  if (ok && std::rename(temporary.c_str(), path.string().c_str()) == 0) return true;
#endif
  std::remove(temporary.c_str());
  return false;
}
inline constexpr uint64_t kMaxPipelineCacheBytes = 256ull * 1024 * 1024;
inline bool ValidPipelineCacheSize(int64_t size) {
  return size >= 0 && uint64_t(size) <= kMaxPipelineCacheBytes;
}
}
