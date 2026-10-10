// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace xbox360ps5::gpu_diag {
enum class Kind : unsigned { pipeline, submit, present, fence, idle, cpu_draw, cpu_copy, cpu_upload, cpu_completion, count };
inline constexpr const char* names[] = {"pipeline", "submit", "present", "fence", "idle", "cpu-draw-sampled-1of1024", "cpu-copy", "cpu-upload-sampled-1of1024", "cpu-completion"};
inline std::atomic<bool> enabled{false};
inline std::atomic<bool> stages_enabled{false};
// How far around a guest write the GPU's copy of memory is invalidated on
// speculation, in bytes (a power of two; 0 keeps the core's own 256 KiB
// block). Read on every write fault, so it can be changed while a game runs
// to compare the same scene. 16 KiB is one host page: in the experimental.7
// session it had the fewest watch closes on the GPU thread (about 2,500 a
// second against 4,200 at 64 KiB and 5,300 at 256 KiB), with more faults.
inline std::atomic<uint32_t> invalidation_window{0x4000};
// Whether a range request whose pages are all valid is answered without the
// global critical region (see RangeBitsAllSet). On by default in experimental
// builds; can be switched while a game runs to compare the same scene.
inline std::atomic<bool> lock_free_valid{true};
// Whether memory that the guest keeps rewriting is left unwatched and compared
// by content when a draw asks for it (xenia/gpu/shared_memory.cc). Can be
// switched while a game runs: off, such pages go back to being watched as they
// are next uploaded.
inline std::atomic<bool> unwatched_pages{true};
// Whether a draw may take its data from guest memory the game has released
// (pages its heap marks as not accessible), when the host can still read them.
inline std::atomic<bool> read_released_pages{false};
// A folder for the bytes of small resolves and small textures (a diagnosis asked
// for by a file in the title's folder), or null. Set before the game starts.
inline const char* dump_folder = nullptr;
// Draws made with the stand-in pixel shader of asynchronous pipeline creation
// (their real pipeline was still being compiled), and draws that waited for
// the real one instead. For the periodic performance line.
inline std::atomic<unsigned long long> stand_in_draws{0}, pipeline_waits{0};
// True when bits first..last of a bitmap of 64-bit words are all set. The words
// are written by other threads under a lock and only read here: a request that
// sees a page valid just before a guest write invalidates it is a draw ordered
// before that write, which the locked check allows as well.
inline bool RangeBitsAllSet(const uint64_t* words, uint32_t first, uint32_t last) {
  const uint32_t block_first = first >> 6, block_last = last >> 6;
  for (uint32_t block = block_first; block <= block_last; ++block) {
    uint64_t need = UINT64_MAX;
    if (block == block_first) need &= ~((uint64_t(1) << (first & 63)) - 1);
    if (block == block_last && (last & 63) != 63) need &= (uint64_t(1) << ((last & 63) + 1)) - 1;
    if ((__atomic_load_n(&words[block], __ATOMIC_ACQUIRE) & need) != need) return false;
  }
  return true;
}
struct Reading { uint64_t calls = 0, nanoseconds = 0, worst = 0, errors = 0; };
struct Counter {
  std::atomic<uint64_t> calls{0}, nanoseconds{0}, worst{0}, errors{0};
  void Add(uint64_t ns, bool failed) {
    calls.fetch_add(1, std::memory_order_relaxed);
    nanoseconds.fetch_add(ns, std::memory_order_relaxed);
    if (failed) errors.fetch_add(1, std::memory_order_relaxed);
    auto old = worst.load(std::memory_order_relaxed);
    while (old < ns && !worst.compare_exchange_weak(old, ns, std::memory_order_relaxed)) {}
  }
  Reading Take() {
    // Counters may straddle one summary boundary; no render-thread lock.
    return {calls.exchange(0, std::memory_order_relaxed),
            nanoseconds.exchange(0, std::memory_order_relaxed),
            worst.exchange(0, std::memory_order_relaxed),
            errors.exchange(0, std::memory_order_relaxed)};
  }
};
inline std::array<Counter, static_cast<unsigned>(Kind::count)> counters;
// Inclusive host wall time: nested scopes overlap and must not be added.
// Draw/upload readings count only timed samples; totals are not full workload totals.
// Disabled mode does not read the clock or update counters.
class Scope {
 public:
  explicit Scope(Kind kind) : kind_(kind), active_(enabled.load(std::memory_order_relaxed) && stages_enabled.load(std::memory_order_relaxed)) {
    // These paths may run over 100,000 times per second. Clock reads and
    // atomic accounting on every invocation would distort the workload.
    if (active_ && (kind == Kind::cpu_draw || kind == Kind::cpu_upload)) {
      static thread_local std::array<uint64_t, static_cast<unsigned>(Kind::count)> seen{};
      active_ = (seen[static_cast<unsigned>(kind)]++ & 1023) == 0;
    }
    if (active_) started_ = std::chrono::steady_clock::now();
  }
  ~Scope() {
    if (!active_) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_).count();
    counters[static_cast<unsigned>(kind_)].Add(uint64_t(ns), false);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
 private:
  Kind kind_;
  bool active_;
  std::chrono::steady_clock::time_point started_{};
};

template<class Fn, class... Args>
auto Call(Kind kind, Fn fn, Args&&... args) {
  if (!enabled.load(std::memory_order_relaxed)) return fn(std::forward<Args>(args)...);
  const auto started = std::chrono::steady_clock::now();
  const auto result = fn(std::forward<Args>(args)...);
  const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - started).count();
  counters[static_cast<unsigned>(kind)].Add(uint64_t(ns), int(result) < 0);
  return result;
}
inline void Reset() { for (auto& c : counters) c.Take(); }
// These are API wall times, including waits; they are not GPU utilization.

// PS5X360E: frame pacing. Intervals between events, bucketed in display
// refreshes (16.68 ms at 59.94 Hz). One writer thread per instance.
inline uint64_t SteadyNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}
struct Intervals {
  // Under 0.75, about 1, 1.25-1.75, about 2, 2.25-2.75, about 3, about 4, over
  // 4.5 refreshes.
  static constexpr double kEdgesMs[7] = {12.5, 20.8, 29.2, 37.5, 45.8, 54.2, 75.0};
  std::atomic<uint64_t> last{0}, count{0}, sum_ns{0}, sum_sq_us2{0}, max_ns{0};
  std::array<std::atomic<uint64_t>, 8> buckets{};
  void Mark(uint64_t now = SteadyNs()) {
    const uint64_t prev = last.exchange(now, std::memory_order_relaxed);
    if (!prev || now <= prev) return;
    const uint64_t d = now - prev;
    if (d > 1000000000ull) return;  // Pauses and loading.
    count.fetch_add(1, std::memory_order_relaxed);
    sum_ns.fetch_add(d, std::memory_order_relaxed);
    sum_sq_us2.fetch_add((d / 1000) * (d / 1000), std::memory_order_relaxed);
    uint64_t m = max_ns.load(std::memory_order_relaxed);
    while (m < d && !max_ns.compare_exchange_weak(m, d, std::memory_order_relaxed)) {}
    unsigned b = 0;
    while (b < 7 && double(d) / 1e6 >= kEdgesMs[b]) ++b;
    buckets[b].fetch_add(1, std::memory_order_relaxed);
  }
  // "n 900 mean 33.37 ms sd 1.10 max 50.1 | refreshes <1 0, 1 0, 1.5 1, 2 897, 2.5 1, 3 1, 4 0, >4 0"
  std::string TakeText() {
    const uint64_t n = count.exchange(0), s = sum_ns.exchange(0),
                   q = sum_sq_us2.exchange(0), mx = max_ns.exchange(0);
    std::array<unsigned long long, 8> h{};
    for (unsigned i = 0; i < 8; ++i) h[i] = buckets[i].exchange(0);
    const double mean_us = n ? double(s) / 1e3 / double(n) : 0;
    const double var = n ? double(q) / double(n) - mean_us * mean_us : 0;
    char text[256];
    std::snprintf(text, sizeof(text),
                  "n %llu mean %.2f ms sd %.2f max %.1f | in refreshes <1 %llu, 1 %llu, "
                  "1.5 %llu, 2 %llu, 2.5 %llu, 3 %llu, 4 %llu, >4 %llu",
                  (unsigned long long)n, mean_us / 1e3, var > 0 ? std::sqrt(var) / 1e3 : 0.0,
                  double(mx) / 1e6, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
    return text;
  }
};
// Guest vblank interrupts, the game's VdSwap calls, swaps the GPU thread
// processed, and host presents.
inline Intervals vblank_intervals, guest_vdswap, guest_swaps, host_presents;
// Host refreshes (presents) each distinct guest frame got: [0] never shown,
// then 1, 2, 3, 4 or more.
inline std::array<std::atomic<unsigned long long>, 5> shown_refreshes{};
}
