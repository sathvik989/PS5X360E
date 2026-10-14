// SPDX-License-Identifier: MIT
// alpha.88: the driver's waits for the GPU and the clock it reads them with
// (platform/ps5/driver_wait.cpp).
#pragma once
#include <cstdint>
namespace xbox360ps5 {
// "Faster GPU waits": the driver's clock from the time-stamp counter (once on,
// for the rest of the run) and its sleeps in a wait for the GPU 100
// microseconds at most. Set before the game starts.
void SetDriverWaitTuning(bool on);
bool DriverWaitTuning();
// Since the last call: the driver's clock reads, its sleeps in waits for the
// GPU, and the time those asked for and took.
struct DriverWaitCounts {
  uint64_t clock_reads = 0, sleeps = 0, sleep_asked_ns = 0, sleep_taken_ns = 0;
};
DriverWaitCounts TakeDriverWaitCounts();
}  // namespace xbox360ps5
