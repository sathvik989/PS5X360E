// SPDX-License-Identifier: MIT
// Which CPUs a thread of the title runs on. On the console a thread starts with
// the CPUs of the thread that made it. Measured on firmware 13.60: the title's
// first thread has CPUs 0 to 12 (mask 0x1fff) at priority 700, every thread of
// the emulator inherits them, and in five seconds each busy thread was seen on
// all thirteen: the system moves them about freely, and two threads can share
// the two halves of one core.
//
// The experiment here gives the GPU command thread, which the guest waits for,
// a core to itself: it stays on CPU 2, and no other thread of the title may use
// CPU 2 or its other half, CPU 3 (CPUs come in pairs, 0-1, 2-3, ...; the first
// pair is left alone because the system's own work lands there).
//
// alpha.88: the fixed layout (ThreadLayout). Every thread of the title takes
// CPUs by its role, which its name gives: each of the six emulated CPUs and the
// GPU command thread a CPU of its own, the threads that keep time and sound
// theirs, and the rest together on the CPUs left. The system no longer moves the
// busy threads about (their caches stay warm), and a game thread spinning at
// full speed no longer lands on the CPU that keeps vblank or sound.
#include "xbox360ps5/thread_place.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <pthread.h>
extern "C" {
int scePthreadGetaffinity(pthread_t, unsigned long long*);
int scePthreadSetaffinity(pthread_t, unsigned long long);
int scePthreadGetprio(pthread_t, int*);
int scePthreadSetprio(pthread_t, int);
int sceKernelGetCurrentCpu(void);
}
namespace xbox360ps5 {
namespace {
constexpr unsigned long long kTitleCpus = 0x1fffull;  // CPUs 0 to 12.
constexpr unsigned long long kGpuCore = 0xcull;       // CPUs 2 and 3: one core.
constexpr unsigned long long kGpuCpu = 0x4ull;        // CPU 2.
// Off: experimental.10 measured no difference with it on (24 to 26 frames a
// second at five switches in one scene).
std::atomic<bool> dedicated{false};
std::atomic<bool> layout{false};
std::atomic<pthread_t> gpu_thread{nullptr};
std::atomic<unsigned> placed{0}, refused{0};
pthread_t Of(void* thread) { return thread ? static_cast<pthread_t>(thread) : pthread_self(); }

// The layout's roles and their CPUs. CPUs pair up as the two halves of a core
// (0-1, 2-3, ...); 0 to 7 are one cluster of four cores sharing a cache, 8 to 12
// the other (13 to 15 are the system's), and the system's own work lands on 0-1.
// The busiest emulated CPU is nearly always the first (the game's main thread),
// so it has core 2 to itself apart from the light timing threads on its other
// half; the GPU command thread, which it feeds, is next door on core 1 with the
// thread that hands its batches to the driver.
//
// A thread with a CPU of its own may also use the shared ones (0, 1 and 12).
// No other thread may use its CPU, so it stays there; but a thread starts with
// the CPUs of the thread that made it, and the console's scheduler does not
// share a CPU between a thread that runs flat out and another that can run
// nowhere else (base/threading_posix.cc, set_affinity_mask: a game's main
// thread spun for ever waiting for a thread that never ran). An emulated CPU
// runs flat out, and the threads it makes (an I/O worker, a socket's) must be
// able to start elsewhere and take their own place.
enum class Role { kOther, kGuestCpu, kGpu, kSubmit, kPresent, kTiming, kAudio, kDispatch, kIo, kWorker };
constexpr unsigned long long Cpu(int n) { return 1ull << n; }
constexpr unsigned long long kGuestCpuCpus[6] = {Cpu(4), Cpu(6), Cpu(7), Cpu(8), Cpu(9), Cpu(10)};
constexpr unsigned long long kOtherCpus = Cpu(0) | Cpu(1) | Cpu(12);
unsigned long long RoleCpus(Role role, int index) {
  switch (role) {
    case Role::kGuestCpu: return (index >= 0 && index < 6 ? kGuestCpuCpus[index] : 0) | kOtherCpus;
    case Role::kGpu: return Cpu(2) | kOtherCpus;
    case Role::kSubmit: return Cpu(3) | kOtherCpus;
    case Role::kTiming: return Cpu(5) | kOtherCpus;
    case Role::kAudio: return Cpu(11) | kOtherCpus;
    case Role::kPresent: return Cpu(1) | Cpu(12);
    case Role::kDispatch: return Cpu(1) | Cpu(12);
    case Role::kIo: return Cpu(0) | Cpu(1);
    // Bursts of compiling (shaders, pipelines, guest code) mostly while loading,
    // when the emulated CPUs wait: every CPU but theirs, the GPU thread's and
    // the sound's (a burst in play would otherwise cut the sound up).
    case Role::kWorker: return Cpu(0) | Cpu(1) | Cpu(3) | Cpu(5) | Cpu(12);
    default: return kOtherCpus;
  }
}
const char* RoleName(Role role) {
  switch (role) {
    case Role::kGuestCpu: return "emulated CPU";
    case Role::kGpu: return "GPU commands";
    case Role::kSubmit: return "GPU submission";
    case Role::kPresent: return "presentation";
    case Role::kTiming: return "timing";
    case Role::kAudio: return "sound";
    case Role::kDispatch: return "kernel dispatch";
    case Role::kIo: return "I/O";
    case Role::kWorker: return "compiling";
    default: return "other";
  }
}
bool Starts(const char* name, const char* prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; }
Role RoleOf(const char* name, int* index) {
  *index = 0;
  if (!name || !*name) return Role::kOther;
  if (Starts(name, "Guest CPU ")) {
    const char digit = name[10];
    if (digit >= '0' && digit <= '5') { *index = digit - '0'; return Role::kGuestCpu; }
    return Role::kOther;
  }
  if (Starts(name, "GPU Commands")) return Role::kGpu;
  if (Starts(name, "GPU Frame limiter") || Starts(name, "Guest Scheduler Watchdog") ||
      Starts(name, "xe::threading::TimerQueue")) return Role::kTiming;
  if (Starts(name, "XMA Decoder") || Starts(name, "Audio Worker") || Starts(name, "Xbox360PS5 audio") ||
      Starts(name, "Audio Media Player")) return Role::kAudio;
  if (Starts(name, "Kernel Dispatch")) return Role::kDispatch;
  if (Starts(name, "Guest I/O") || Starts(name, "Content trash") || Starts(name, "Logging Writer") ||
      Starts(name, "Shader Storage Writer")) return Role::kIo;
  if (Starts(name, "Shader Translation") || Starts(name, "Shader Analysis") || Starts(name, "Vulkan Pipelines") ||
      Starts(name, "Precompile ")) return Role::kWorker;
  return Role::kOther;
}

// What the layout placed, for ThreadLayoutReport: the threads named so far.
struct Placement { char name[40]; Role role; unsigned long long cpus; bool done; };
std::mutex placements_mutex;
Placement placements[64];
int placement_count = 0;
void Note(const char* name, Role role, unsigned long long cpus, bool done) {
  std::lock_guard<std::mutex> lock(placements_mutex);
  // A name placed again (a role thread renamed, a pool grown) replaces its line.
  for (int i = 0; i < placement_count; ++i) {
    if (std::strncmp(placements[i].name, name, sizeof(placements[i].name) - 1) == 0) {
      placements[i].role = role; placements[i].cpus = cpus; placements[i].done = done;
      return;
    }
  }
  if (placement_count >= int(sizeof(placements) / sizeof(placements[0]))) return;
  Placement& entry = placements[placement_count++];
  std::snprintf(entry.name, sizeof(entry.name), "%s", name);
  entry.role = role; entry.cpus = cpus; entry.done = done;
}
bool SetCpus(pthread_t thread, unsigned long long wanted) {
  unsigned long long have = 0;
  if (scePthreadGetaffinity(thread, &have) == 0 && have == wanted) return true;
  if (scePthreadSetaffinity(thread, wanted) == 0) { ++placed; return true; }
  ++refused;
  return false;
}
void PlaceAs(pthread_t thread, Role role, int index, const char* name) {
  const unsigned long long cpus = RoleCpus(role, index);
  const bool done = SetCpus(thread, cpus);
  if (name && *name) Note(name, role, cpus, done);
}
void Place(pthread_t thread, bool gpu) {
  if (layout.load(std::memory_order_relaxed)) {
    // Until its name says otherwise.
    PlaceAs(thread, gpu ? Role::kGpu : Role::kOther, 0, gpu ? "GPU Commands" : nullptr);
    return;
  }
  const bool own_core = dedicated.load(std::memory_order_relaxed);
  // Without the dedicated core nothing is changed: a thread keeps the CPUs the
  // system gave it, whatever they are on this firmware (as before v0.5.6).
  if (!own_core) return;
  const unsigned long long wanted = !own_core ? kTitleCpus : gpu ? kGpuCpu : kTitleCpus & ~kGpuCore;
  SetCpus(thread, wanted);
}

// A thread starts with the CPUs of the thread that made it, which under the
// layout can be one CPU kept for something else. The emulator's own threads
// take their place as they start (base/threading_posix.cc) and again when
// named; the driver's are made by Mesa's thrd_create (the thread that starts
// the GPU on each submission, made by the first thread to submit: the GPU
// thread or the submission thread; the display's; its compile queues), which
// the link wraps (tools/build-canary-game.sh; pthread_create itself is the
// platform's wrap, for the driver's thread stacks): they start among the
// others.
struct Start { int (*routine)(void*); void* argument; };
std::atomic<unsigned> driver_threads{0};
int Begin(void* raw) {
  const Start start = *static_cast<Start*>(raw);
  delete static_cast<Start*>(raw);
  PlaceAs(pthread_self(), Role::kOther, 0, nullptr);
  ++driver_threads;
  return start.routine(start.argument);
}
}
}  // namespace xbox360ps5

// Without the layout the call goes straight through. thrd_success is 0.
extern "C" int __real_thrd_create(void* thread, int (*routine)(void*), void* argument);
extern "C" int __wrap_thrd_create(void* thread, int (*routine)(void*), void* argument) {
  using namespace xbox360ps5;
  if (!layout.load(std::memory_order_relaxed)) return __real_thrd_create(thread, routine, argument);
  Start* start = new (std::nothrow) Start{routine, argument};
  if (!start) return __real_thrd_create(thread, routine, argument);
  const int result = __real_thrd_create(thread, Begin, start);
  if (result != 0) delete start;
  return result;
}

namespace xbox360ps5 {
int CurrentCpu() { return sceKernelGetCurrentCpu(); }
unsigned long long ThreadCpus(void* thread) {
  unsigned long long mask = 0;
  return scePthreadGetaffinity(Of(thread), &mask) == 0 ? mask : 0;
}
int ThreadPriority(void* thread) {
  int priority = -1;
  return scePthreadGetprio(Of(thread), &priority) == 0 ? priority : -1;
}
void DescribeThread(char* out, size_t size) {
  std::snprintf(out, size, "cpu %d, priority %d, cpus 0x%llx", CurrentCpu(), ThreadPriority(nullptr), ThreadCpus(nullptr));
}
void PlaceCallingThread(bool gpu) {
  if (gpu) gpu_thread = pthread_self();
  Place(pthread_self(), gpu);
}
void PlaceThread(void* thread) {
  if (!thread) return;
  const pthread_t handle = static_cast<pthread_t>(thread);
  Place(handle, handle == gpu_thread.load());
}
void SetGpuCoreDedicated(bool on) {
  dedicated = on;
  if (const pthread_t gpu = gpu_thread.load()) Place(gpu, true);
}
bool GpuCoreDedicated() { return dedicated; }
void SetThreadLayout(bool on) { layout = on; }
bool ThreadLayoutOn() { return layout; }
void PlaceNamedThread(void* thread, const char* name) {
  if (!layout.load(std::memory_order_relaxed)) return;
  int index = 0;
  const Role role = RoleOf(name, &index);
  PlaceAs(Of(thread), role, index, name);
}
void PlaceCallingThreadAsPresenter() {
  if (layout.load(std::memory_order_relaxed)) PlaceAs(pthread_self(), Role::kPresent, 0, "main (presentation)");
}
void PlaceCallingThreadAsSubmitter() {
  if (layout.load(std::memory_order_relaxed)) PlaceAs(pthread_self(), Role::kSubmit, 0, "GPU submission");
}
std::string ThreadLayoutReport() {
  std::lock_guard<std::mutex> lock(placements_mutex);
  std::string out;
  char line[120];
  for (int i = 0; i < placement_count; ++i) {
    const Placement& entry = placements[i];
    std::snprintf(line, sizeof(line), "%s%s: %s, cpus 0x%llx%s", out.empty() ? "" : "; ", entry.name,
                  RoleName(entry.role), entry.cpus, entry.done ? "" : " (refused)");
    out += line;
  }
  std::snprintf(line, sizeof(line), "%s%u driver threads with the others (cpus 0x%llx)", out.empty() ? "" : "; ",
                driver_threads.load(), RoleCpus(Role::kOther, 0));
  out += line;
  return out;
}
void ThreadPlaceCounts(unsigned* given, unsigned* not_given) {
  *given = placed; *not_given = refused;
}
SamplerSeat SeatSampler(void* target, int cpu) {
  SamplerSeat seat;
  const pthread_t thread = static_cast<pthread_t>(target), self = pthread_self();
  if (!target || scePthreadGetaffinity(thread, &seat.target_cpus) != 0 || scePthreadGetaffinity(self, &seat.own_cpus) != 0 ||
      scePthreadGetprio(self, &seat.own_priority) != 0) return seat;
  const unsigned long long one = 1ull << cpu;
  if (scePthreadSetaffinity(thread, one) != 0) return seat;
  // 256 is the highest priority a title's thread may have (the range is 256 to 767, lower first).
  if (scePthreadSetaffinity(self, one) != 0 || scePthreadSetprio(self, 256) != 0) {
    scePthreadSetaffinity(thread, seat.target_cpus);
    scePthreadSetaffinity(self, seat.own_cpus);
    return seat;
  }
  seat.seated = true;
  return seat;
}
void UnseatSampler(void* target, const SamplerSeat& seat) {
  if (!seat.seated) return;
  scePthreadSetprio(pthread_self(), seat.own_priority);
  scePthreadSetaffinity(pthread_self(), seat.own_cpus);
  scePthreadSetaffinity(static_cast<pthread_t>(target), seat.target_cpus);
}
}
