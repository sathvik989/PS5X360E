// SPDX-License-Identifier: MIT
// Which CPUs and priority a thread of the title has on the console
// (platform/ps5/thread_place.cpp).
#pragma once
#include <cstddef>
#include <string>
namespace xbox360ps5 {
// The CPU the calling thread is on now.
int CurrentCpu();
// A thread's CPUs as a mask (0 when the system does not say) and its priority
// (-1 likewise). `thread` is a pthread_t; null is the calling thread.
unsigned long long ThreadCpus(void* thread);
int ThreadPriority(void* thread);
// "cpu N, priority P, cpus 0xMASK" for the calling thread.
void DescribeThread(char* out, size_t size);
// Puts the calling thread where the current arrangement wants it: the GPU
// command thread (gpu) on its own core, every other thread off that core. A
// thread calls this when it starts; the GPU command thread calls it again as
// itself.
void PlaceCallingThread(bool gpu);
// The same for another thread (a pthread_t), after the arrangement changed.
void PlaceThread(void* thread);
// Whether the GPU command thread has a core to itself. Changing it moves the
// GPU command thread at once; the others move when PlaceThread is called for
// them.
void SetGpuCoreDedicated(bool on);
bool GpuCoreDedicated();
// How many placements the system accepted and refused.
void ThreadPlaceCounts(unsigned* given, unsigned* not_given);
// alpha.88: the fixed layout (platform/ps5/thread_place.cpp). Set before the
// emulator makes its threads; each then takes the CPUs of its role, which its
// name gives (PlaceNamedThread, called as a thread is named; null is the
// calling thread). The main thread and the GPU submission thread have no name
// and say what they are. ThreadLayoutReport lists the threads placed so far.
void SetThreadLayout(bool on);
bool ThreadLayoutOn();
void PlaceNamedThread(void* thread, const char* name);
void PlaceCallingThreadAsPresenter();
void PlaceCallingThreadAsSubmitter();
std::string ThreadLayoutReport();
// For a sample without the lean towards system calls. On the console a signal
// sent from another CPU is taken when the target next enters the kernel, so a
// thread that makes many system calls is mostly caught in them. With the
// sampler on the target's own CPU and at the highest priority, its wake-up
// preempts the target, which then takes the signal where it was interrupted.
// SeatSampler puts the target (a pthread_t) and the calling thread on one CPU
// and raises the caller; UnseatSampler puts both back as they were.
struct SamplerSeat { unsigned long long target_cpus = 0, own_cpus = 0; int own_priority = -1; bool seated = false; };
SamplerSeat SeatSampler(void* target, int cpu);
void UnseatSampler(void* target, const SamplerSeat& seat);
}
