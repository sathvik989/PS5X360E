// SPDX-License-Identifier: MIT
#pragma once
#include "xenia/vfs/devices/xcontent_container_entry.h"
#include "xenia/vfs/devices/xcontent_container_file.h"
#include "xenia/vfs/devices/host_path_device.h"
#include <chrono>
#include <cstdio>
#include <vector>

namespace xbox360ps5 {
inline int CheckContainerReads() {
  using namespace xe;
  using namespace xe::vfs;
  HostPathDevice device("test:", std::filesystem::temp_directory_path(), true);
  class TestEntry : public XContentContainerEntry {
   public:
    explicit TestEntry(Device* device) : XContentContainerEntry(device, nullptr, "test.bin") {}
    X_STATUS Open(uint32_t, File**) override { return X_STATUS_NOT_IMPLEMENTED; }
    void Populate(const std::vector<size_t>& lengths) {
      auto& records = const_cast<std::vector<BlockRecord>&>(block_list());
      size_ = 0;
      for (size_t i = 0; i < lengths.size(); ++i) {
        // Deliberately fragmented physical positions and multiple files.
        records.push_back({i % 3, i * 16384 + 13, lengths[i]});
        size_ += lengths[i];
      }
    }
  };
  class TestFile : public XContentContainerFile {
   public:
    explicit TestFile(TestEntry* entry) : XContentContainerFile(0, entry), source(entry) {
      size_t position = 0;
      for (const auto& block : source->block_list()) {
        logical.push_back(position);
        position += block.length;
      }
    }
    TestEntry* source;
    std::vector<size_t> logical;
    bool fail = false;
   private:
    size_t Read(std::span<uint8_t> buffer, size_t offset, size_t file) override {
      if (fail) return 0;
      const size_t block = (offset - 13) / 16384;
      if (block >= logical.size() || file != block % 3) return 0;
      const size_t within = offset - source->block_list()[block].offset;
      if (within + buffer.size() > source->block_list()[block].length) return 0;
      for (size_t i = 0; i < buffer.size(); ++i) buffer[i] = (logical[block] + within + i) % 251;
      return buffer.size();
    }
  };
  for (const auto& lengths : {std::vector<size_t>{4096, 4096, 23},
                              std::vector<size_t>{2048, 2048, 11},
                              std::vector<size_t>{7, 0, 19, 3},
                              std::vector<size_t>{1}, std::vector<size_t>{}}) {
    TestEntry entry(&device); entry.Populate(lengths);
    TestFile file(&entry);
    for (size_t offset = 0; offset <= entry.size() + 1; ++offset) {
      std::vector<uint8_t> data(31, 0xCC);
      size_t read = 999;
      const auto status = file.ReadSync(data, offset, &read);
      if (offset >= entry.size()) {
        if (status != X_STATUS_END_OF_FILE || read != 0) return 1;
      } else {
        const size_t expected = std::min(data.size(), entry.size() - offset);
        if (status != X_STATUS_SUCCESS || read != expected) return 2;
        for (size_t i = 0; i < read; ++i) if (data[i] != (offset + i) % 251) return 3;
        if (read < data.size() && data[read] != 0xCC) return 4;
      }
    }
    if (entry.size()) {
      size_t read = 999;
      if (file.ReadSync({}, 0, &read) != X_STATUS_SUCCESS || read != 0) return 5;
      file.fail = true;
      uint8_t data;
      if (file.ReadSync({&data, 1}, 0, &read) != X_STATUS_UNSUCCESSFUL || read != 0) return 6;
    }
  }
  TestEntry large(&device);
  large.Populate(std::vector<size_t>(100000, 4096));
  TestFile large_file(&large);
  uint8_t data; size_t read;
  const size_t offset = large.size() - 7;
  const auto start = std::chrono::steady_clock::now();
  for (int n = 0; n < 10000; ++n) {
    if (large_file.ReadSync({&data, 1}, offset, &read) != X_STATUS_SUCCESS ||
        read != 1 || data != offset % 251) return 7;
  }
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start).count();
  std::printf("PASS: STFS/SVOD block sizes, fragments, varying/zero blocks, EOF, empty buffer, read failures; 10000 late reads in %lld us (host fixture, not game FPS)\n", (long long)micros);
  return 0;
}
}
