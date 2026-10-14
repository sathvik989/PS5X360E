// SPDX-License-Identifier: MIT
// alpha.88: the driver's waits for the GPU, and its clock (Mesa's PS5 winsys,
// linked into the title: src/amd/vulkan/winsys/ps5/radv_ps5_winsys.c and
// src/util/os_time.c in PS5_Mesa).
//
// radv_ps5_queue_wait_seq looks at the GPU's progress in a loop for 1.5 ms,
// then sleeps a millisecond at a time (radv_ps5_sleep_us), so a wait that ends
// just past 1.5 ms is seen a millisecond or more late; the emulator's GPU
// thread waits that way for what the guest reads back. The driver asks for the
// time with os_time_get_nano at each turn of that loop and nine times or so for
// each submission, and each is a clock_gettime system call on the console,
// about a microsecond.
//
// The link wraps both (tools/build-canary-game.sh). Each is written out in full
// here as the driver has it, with no call to the driver's own, and with
// "Faster GPU waits" (SetDriverWaitTuning):
// - the time comes from the processor's time-stamp counter, which a title reads
//   in user mode, scaled to nanoseconds (as xe::Clock does on the console,
//   base/clock_posix.cc). It starts a little ahead of the system's monotonic
//   clock, and a thread never reads an earlier time than it read before: the
//   driver takes a time that goes back as a GPU that stopped (its progress
//   check subtracts unsigned times). Once on, it stays on for the run.
// - a sleep in a wait for the GPU is 100 microseconds at most.
// Counted either way, for the performance summary: clock reads, and the sleeps
// with the time they asked for and the time they took.
#include "xbox360ps5/driver_wait.hpp"
#include <atomic>
#include <ctime>
#include <mutex>
extern "C" {
uint64_t sceKernelReadTsc(void);
uint64_t sceKernelGetTscFrequency(void);
}
namespace xbox360ps5 {
namespace {
constexpr unsigned kSleepCapUs = 100;
std::atomic<bool> tuning{false};
// The counter's time, from StartCounterClock.
std::atomic<bool> counter_clock{false};
uint64_t base_ticks = 0, base_ns = 0, ns_per_tick_32 = 0;
std::once_flag counter_clock_once;
std::atomic<uint64_t> clock_reads{0}, sleeps{0}, sleep_asked_ns{0}, sleep_taken_ns{0};

uint64_t SystemNs() {
  timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return uint64_t(now.tv_sec) * 1000000000ull + uint64_t(now.tv_nsec);
}
void StartCounterClock() {
  const uint64_t frequency = sceKernelGetTscFrequency();
  if (!frequency) return;
  // The counter first, then the system clock: the counter's time is never
  // behind a time the system clock gave before.
  base_ticks = sceKernelReadTsc();
  base_ns = SystemNs();
  ns_per_tick_32 = uint64_t((__uint128_t(1000000000ull) << 32) / frequency);
  counter_clock.store(true, std::memory_order_release);
}
uint64_t Now() {
  if (!counter_clock.load(std::memory_order_acquire)) return SystemNs();
  thread_local uint64_t last = 0;
  const uint64_t ticks = sceKernelReadTsc() - base_ticks;
  uint64_t now = base_ns + uint64_t((__uint128_t(ticks) * ns_per_tick_32) >> 32);
  if (now < last) now = last;
  last = now;
  return now;
}
// For timing the sleeps, whichever clock the driver reads.
uint64_t CounterNs(uint64_t ticks) {
  static const uint64_t frequency = sceKernelGetTscFrequency();
  return frequency ? uint64_t(__uint128_t(ticks) * 1000000000ull / frequency) : 0;
}
}  // namespace

void SetDriverWaitTuning(bool on) {
  if (on) std::call_once(counter_clock_once, StartCounterClock);
  tuning.store(on, std::memory_order_relaxed);
}
bool DriverWaitTuning() { return tuning.load(std::memory_order_relaxed); }
DriverWaitCounts TakeDriverWaitCounts() {
  DriverWaitCounts counts;
  counts.clock_reads = clock_reads.exchange(0, std::memory_order_relaxed);
  counts.sleeps = sleeps.exchange(0, std::memory_order_relaxed);
  counts.sleep_asked_ns = sleep_asked_ns.exchange(0, std::memory_order_relaxed);
  counts.sleep_taken_ns = sleep_taken_ns.exchange(0, std::memory_order_relaxed);
  return counts;
}
}  // namespace xbox360ps5

extern "C" int64_t __wrap_os_time_get_nano(void) {
  using namespace xbox360ps5;
  // Counted in batches: the driver's wait loop reads the clock flat out.
  thread_local uint32_t unreported = 0;
  if (++unreported == 64) {
    clock_reads.fetch_add(64, std::memory_order_relaxed);
    unreported = 0;
  }
  return int64_t(Now());
}

extern "C" void __wrap_radv_ps5_sleep_us(unsigned microseconds) {
  using namespace xbox360ps5;
  if (tuning.load(std::memory_order_relaxed) && microseconds > kSleepCapUs) microseconds = kSleepCapUs;
  const timespec delay = {time_t(microseconds / 1000000u), long(microseconds % 1000000u) * 1000l};
  const uint64_t before = sceKernelReadTsc();
  nanosleep(&delay, nullptr);
  const uint64_t taken = CounterNs(sceKernelReadTsc() - before);
  sleeps.fetch_add(1, std::memory_order_relaxed);
  sleep_asked_ns.fetch_add(uint64_t(microseconds) * 1000ull, std::memory_order_relaxed);
  sleep_taken_ns.fetch_add(taken, std::memory_order_relaxed);
}
