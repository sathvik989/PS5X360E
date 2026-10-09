// SPDX-License-Identifier: MIT
// Xenia's memory contract on the console. Measured on hardware (FW 13.60):
// anonymous mmap, PROT_NONE included, is charged to a 448 MiB flexible budget,
// so every mapping here is direct memory inside a reserved virtual range;
// execute is granted only by a protection change after the mapping; kernel
// pages are 16 KiB while guest heaps use 4 KiB pages, so each host page gets
// the union of the access of its four guest pages.
#include "xenia/base/memory.h"
#include "xbox360ps5/gpu_diagnostics.hpp"
#include <algorithm>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <vector>
extern "C" {
int32_t sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t*);
int32_t sceKernelReleaseDirectMemory(int64_t, size_t);
int32_t sceKernelMapDirectMemory(void**, size_t, int, int, int64_t, size_t);
int64_t sceKernelGetDirectMemorySize();
int32_t sceKernelMprotect(const void*, size_t, int);
int32_t sceKernelReserveVirtualRange(void**, size_t, int, size_t);
int32_t sceKernelMunmap(void*, size_t);
int32_t sceKernelVirtualQuery(const void*, int, void*, size_t);
int sceKernelDebugOutText(int, const char*);
}
// Counters for a performance measurement (read by the title's guide): calls to
// Protect, kernel protection calls they made, and the time inside those.
namespace xbox360ps5 {
std::atomic<unsigned long long> protect_calls{0}, protect_syscalls{0}, protect_nanoseconds{0}, fault_count{0};
// The same, for the protections that open pages for writing (after a fault)
// and for the pages each kind covered.
std::atomic<unsigned long long> open_syscalls{0}, open_nanoseconds{0}, protect_pages{0};
}
namespace xe::memory {
namespace {
constexpr size_t kPage = 0x4000, kUnit = 0x10000, kGuestPage = 0x1000;
constexpr size_t kPerPage = kPage / kGuestPage;
constexpr uint8_t kNoHold = 0xFF;
constexpr int kFixed = 0x10, kReadWrite = 3, kCpuMemory = 12;
// Unplaced mappings land in the Vulkan driver's GPU window (0x2_0000_0000 to
// 0x2_FFFF_FFFF); the title's heap starts at 0x20_0000_0000. Mappings without
// a required address go between the guest address space and that heap.
constexpr uintptr_t kAnywhereStart = 0x1400000000ull, kAnywhereEnd = 0x2000000000ull;
// PS5X360E: direct memory is physical from the moment it is allocated, so
// Xenia's 4.5 GiB guest mapping (its 4 GiB virtual space, then the 512 MiB of
// guest RAM at 4 GiB) took about 6 GiB of the console's 12 before a game ran,
// and rendering at twice the resolution ran out of memory. Below lazy_limit an
// object gets memory a chunk at a time, when a view of it is first committed;
// above it (the guest RAM, which the GPU imports whole) all of it up front.
constexpr size_t kChunk = 0x100000;
constexpr size_t kLazyFrom = size_t(1) << 32, kLazyLimit = size_t(1) << 32;
struct Object {
  int64_t start;   // Of the up-front part: offset lazy_limit on.
  size_t bytes;
  size_t lazy_limit = 0;
  std::vector<int64_t> chunks;  // Below lazy_limit; -1 until committed.
};
struct View {
  size_t size; int handle; bool owns_object;
  size_t offset = 0;              // In the object.
  std::vector<uint8_t> backed;    // Per kernel page: memory mapped there.
  std::vector<uint8_t> access;  // Per 4 KiB guest page.
  std::vector<uint8_t> host;    // Protection in effect per kernel page.
  // A protection asked for whole kernel pages (the emulator's write watches
  // come this way) holds until the next one: a commit or a release of one
  // guest page beside it must not open the kernel page again, or writes to
  // watched memory go unseen and the GPU keeps stale textures (frames of a
  // video flashed green). kNoHold: the union of the guest pages applies.
  std::vector<uint8_t> hold;
};
std::mutex guard;
std::map<int, Object> objects;
std::map<uintptr_t, View> views;
int next_handle = 1;
uintptr_t anywhere = kAnywhereStart;

size_t RoundUp(size_t value, size_t unit) { return (value + unit - 1) / unit * unit; }
void Note(const char* what, uintptr_t address, size_t bytes, int result) {
  static std::atomic<unsigned> notes{0};
  if (notes.fetch_add(1, std::memory_order_relaxed) >= 60) return;
  char text[160];
  std::snprintf(text, sizeof(text), "[X360] MEMORY %s address=%llx bytes=%llx result=%x\n", what,
                static_cast<unsigned long long>(address), static_cast<unsigned long long>(bytes),
                static_cast<unsigned>(result));
  sceKernelDebugOutText(0, text);
}
auto Containing(uintptr_t address, size_t length) {
  auto i = views.upper_bound(address);
  if (i == views.begin()) return views.end();
  --i;
  const size_t offset = address - i->first;
  return offset < i->second.size && length <= i->second.size - offset ? i : views.end();
}
bool RangeFree(uintptr_t address, size_t bytes) {
  struct { void* start; void* end; int64_t offset; int protection; int type; unsigned flags; char name[32]; } info{};
  if (sceKernelVirtualQuery(reinterpret_cast<void*>(address), 1, &info, sizeof(info))) return true;
  return reinterpret_cast<uintptr_t>(info.start) >= address + bytes;
}
// A reserved range exactly at `wanted`, or anywhere suitable when it is zero.
void* Reserve(uintptr_t wanted, size_t bytes) {
  if (wanted) {
    void* at = reinterpret_cast<void*>(wanted);
    const int32_t result = sceKernelReserveVirtualRange(&at, bytes, 0, kPage);
    if (result == 0) {
      if (at == reinterpret_cast<void*>(wanted)) return at;
      Note("reserve-moved", reinterpret_cast<uintptr_t>(at), bytes, 0);
      sceKernelMunmap(at, bytes);
    } else {
      Note("reserve", wanted, bytes, result);
    }
    // The kernel moved the hint. Below 4 GiB, take a verified free range.
    if (wanted + bytes <= 0x100000000ull && RangeFree(wanted, bytes)) {
      at = reinterpret_cast<void*>(wanted);
      if (sceKernelReserveVirtualRange(&at, bytes, kFixed, kPage) == 0) {
        if (at == reinterpret_cast<void*>(wanted)) return at;
        sceKernelMunmap(at, bytes);
      }
    }
    return nullptr;
  }
  for (int attempt = 0; attempt < 64 && anywhere + bytes <= kAnywhereEnd; ++attempt) {
    void* at = reinterpret_cast<void*>(anywhere);
    if (sceKernelReserveVirtualRange(&at, bytes, 0, kUnit)) break;
    const uintptr_t got = reinterpret_cast<uintptr_t>(at);
    if (got >= kAnywhereStart && got + bytes <= kAnywhereEnd) {
      anywhere = RoundUp(got + bytes, kUnit);
      return at;
    }
    sceKernelMunmap(at, bytes);
    anywhere += RoundUp(bytes, 0x10000000);
  }
  return nullptr;
}
bool AllocateDirect(size_t bytes, int64_t& start) {
  const int32_t result = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
      bytes, kUnit, kCpuMemory, &start);
  if (result) { Note("allocate-direct", 0, bytes, result); return false; }
  return true;
}
bool CreateObject(size_t bytes, Object& object, bool allow_lazy = false) {
  object.bytes = RoundUp(bytes, kUnit);
  object.lazy_limit = allow_lazy && object.bytes > kLazyFrom ? kLazyLimit : 0;
  object.chunks.assign(object.lazy_limit / kChunk, -1);
  return AllocateDirect(object.bytes - object.lazy_limit, object.start);
}
void ReleaseObject(const Object& object) {
  sceKernelReleaseDirectMemory(object.start, object.bytes - object.lazy_limit);
  for (const int64_t chunk : object.chunks)
    if (chunk >= 0) sceKernelReleaseDirectMemory(chunk, kChunk);
}
// Maps [offset, offset + bytes) of an object at a reserved address: the part
// that has memory (lazy chunks not yet committed stay reserved).
bool MapBacked(const Object& object, size_t offset, uintptr_t at, size_t bytes) {
  for (size_t done = 0; done < bytes;) {
    const size_t here = offset + done;
    size_t span;
    int64_t physical;
    if (here < object.lazy_limit) {
      span = std::min(kChunk - here % kChunk, bytes - done);
      physical = object.chunks[here / kChunk];
      if (physical >= 0) physical += int64_t(here % kChunk);
    } else {
      span = bytes - done;
      physical = object.start + int64_t(here - object.lazy_limit);
    }
    if (physical >= 0) {
      void* mapped = reinterpret_cast<void*>(at + done);
      const int32_t result = sceKernelMapDirectMemory(&mapped, span, kReadWrite, kFixed, physical, kPage);
      if (result || mapped != reinterpret_cast<void*>(at + done)) {
        Note("map-direct", at + done, span, result);
        return false;
      }
    }
    done += span;
  }
  return true;
}
void* MapObject(const Object& object, size_t offset, uintptr_t wanted, size_t bytes) {
  void* reserved = Reserve(wanted, bytes);
  if (!reserved) return nullptr;
  if (!object.lazy_limit) {
    void* mapped = reserved;
    const int32_t result = sceKernelMapDirectMemory(&mapped, bytes, kReadWrite, kFixed,
        object.start + static_cast<int64_t>(offset), kPage);
    if (result || mapped != reserved) {
      Note("map-direct", reinterpret_cast<uintptr_t>(reserved), bytes, result);
      sceKernelMunmap(result ? reserved : mapped, bytes);
      return nullptr;
    }
    return mapped;
  }
  if (!MapBacked(object, offset, reinterpret_cast<uintptr_t>(reserved), bytes)) {
    sceKernelMunmap(reserved, bytes);
    return nullptr;
  }
  return reserved;
}
// Applies the union of the guest pages' access to the kernel pages of a range.
bool Apply(uintptr_t base, View& view, size_t first_guest, size_t guest_count) {
  const size_t first = first_guest / kPerPage, last = (first_guest + guest_count - 1) / kPerPage;
  bool good = true;
  for (size_t page = first; page <= last;) {
    const auto wanted = [&](size_t n) {
      if (view.hold[n] != kNoHold) return view.hold[n];
      uint8_t bits = 0;
      for (size_t g = 0; g < kPerPage; ++g) bits |= view.access[n * kPerPage + g];
      return bits;
    };
    const uint8_t bits = wanted(page);
    if (!view.backed[page] || view.host[page] == bits) { ++page; continue; }
    size_t end = page + 1;
    if (xbox360ps5::gpu_diag::enabled.load(std::memory_order_relaxed)) {
      // Already-correct pages do not split an otherwise identical target
      // protection. Include them only when bridging to another changed page;
      // do not expand beyond the requested range or include an unchanged tail.
      for (size_t scan = end; scan <= last && view.backed[scan] && wanted(scan) == bits; ++scan)
        if (view.host[scan] != bits) end = scan + 1;
    } else {
      while (end <= last && view.backed[end] && view.host[end] != bits && wanted(end) == bits) ++end;
    }
    timespec before{}, after{};
    clock_gettime(CLOCK_MONOTONIC, &before);
    const int32_t result = sceKernelMprotect(reinterpret_cast<void*>(base + page * kPage),
                                             (end - page) * kPage, bits);
    clock_gettime(CLOCK_MONOTONIC, &after);
    const long long taken = (after.tv_sec - before.tv_sec) * 1000000000ll + (after.tv_nsec - before.tv_nsec);
    ++xbox360ps5::protect_syscalls;
    xbox360ps5::protect_nanoseconds += taken;
    xbox360ps5::protect_pages += end - page;
    if (bits & 2) { ++xbox360ps5::open_syscalls; xbox360ps5::open_nanoseconds += taken; }
    if (result) { Note("protect", base + page * kPage, (end - page) * kPage, result); good = false; }
    else std::fill(view.host.begin() + page, view.host.begin() + end, bits);
    page = end;
  }
  return good;
}
// Gives the lazy chunks under [first_guest, first_guest + guest_count) of a
// view memory, zeroed, mapped in every view of the object that covers them.
bool EnsureBacked(uintptr_t base, View& view, size_t first_guest, size_t guest_count) {
  auto object = objects.find(view.handle);
  if (object == objects.end() || !object->second.lazy_limit) return true;
  Object& o = object->second;
  const size_t from = view.offset + first_guest * kGuestPage;
  const size_t to = std::min(view.offset + (first_guest + guest_count) * kGuestPage, o.lazy_limit);
  for (size_t chunk = from / kChunk; chunk * kChunk < to; ++chunk) {
    if (o.chunks[chunk] >= 0) continue;
    if (!AllocateDirect(kChunk, o.chunks[chunk])) return false;
    const size_t chunk_from = chunk * kChunk;
    bool zeroed = false;
    for (auto& [other_base, other] : views) {
      if (other.handle != view.handle || other.offset >= chunk_from + kChunk ||
          other.offset + other.size <= chunk_from) continue;
      const size_t lo = std::max(chunk_from, other.offset);
      const size_t hi = std::min(chunk_from + kChunk, other.offset + other.size);
      const uintptr_t at = other_base + (lo - other.offset);
      if (!MapBacked(o, lo, at, hi - lo)) return false;
      if (!zeroed && lo == chunk_from && hi == chunk_from + kChunk) {
        std::memset(reinterpret_cast<void*>(at), 0, kChunk);
        zeroed = true;
      }
      const size_t first_page = (lo - other.offset) / kPage, end_page = (hi - other.offset) / kPage;
      for (size_t page = first_page; page < end_page; ++page) {
        other.backed[page] = 1;
        other.host[page] = kReadWrite;
      }
      Apply(other_base, other, (lo - other.offset) / kGuestPage, (hi - lo) / kGuestPage);
    }
    if (!zeroed) {
      // No view held the whole chunk: zero it through a temporary mapping.
      if (void* whole = Reserve(0, kChunk)) {
        void* mapped = whole;
        if (!sceKernelMapDirectMemory(&mapped, kChunk, kReadWrite, kFixed, o.chunks[chunk], kPage))
          std::memset(mapped, 0, kChunk);
        sceKernelMunmap(whole, kChunk);
      }
    }
  }
  return true;
}
bool RangeBacked(const View& view, size_t first_guest, size_t guest_count) {
  const size_t first = first_guest / kPerPage, last = (first_guest + guest_count - 1) / kPerPage;
  for (size_t page = first; page <= last; ++page) if (!view.backed[page]) return false;
  return true;
}
bool SetAccess(uintptr_t address, size_t length, PageAccess access, PageAccess* previous, bool hold = false) {
  if (!length) return false;
  auto i = Containing(address, length);
  if (i == views.end()) return false;
  if (access != PageAccess::kNoAccess) {
    const size_t first_guest = (address - i->first) / kGuestPage;
    const size_t guest_count = (address - i->first + length + kGuestPage - 1) / kGuestPage - first_guest;
    if (!RangeBacked(i->second, first_guest, guest_count) &&
        !EnsureBacked(i->first, i->second, first_guest, guest_count)) return false;
  }
  const size_t first = (address - i->first) / kGuestPage;
  const size_t count = (address - i->first + length + kGuestPage - 1) / kGuestPage - first;
  if (previous) *previous = static_cast<PageAccess>(i->second.access[first]);
  std::fill_n(i->second.access.begin() + first, count, static_cast<uint8_t>(access));
  // The kernel pages this covers whole take (or lose) a hold.
  const size_t whole_first = (first + kPerPage - 1) / kPerPage, whole_end = (first + count) / kPerPage;
  if (whole_end > whole_first)
    std::fill(i->second.hold.begin() + whole_first, i->second.hold.begin() + whole_end,
              hold ? static_cast<uint8_t>(access) : kNoHold);
  return Apply(i->first, i->second, first, count);
}
void* AddView(void* mapped, size_t bytes, int handle, bool owns, PageAccess access, size_t offset = 0) {
  View view{bytes, handle, owns, offset, std::vector<uint8_t>(bytes / kPage, 1),
            std::vector<uint8_t>(bytes / kGuestPage, static_cast<uint8_t>(access)),
            std::vector<uint8_t>(bytes / kPage, kReadWrite), std::vector<uint8_t>(bytes / kPage, kNoHold)};
  // Lazy chunks without memory yet: nothing mapped, nothing to protect.
  auto object = objects.find(handle);
  if (object != objects.end() && object->second.lazy_limit) {
    for (size_t page = 0; page < bytes / kPage; ++page) {
      const size_t at = offset + page * kPage;
      if (at < object->second.lazy_limit && object->second.chunks[at / kChunk] < 0) {
        view.backed[page] = 0;
        view.host[page] = 0;
      }
    }
  }
  auto i = views.emplace(reinterpret_cast<uintptr_t>(mapped), std::move(view)).first;
  Apply(i->first, i->second, 0, bytes / kGuestPage);
  return mapped;
}
}

size_t page_size() { return kPage; }
size_t allocation_granularity() { return kPage; }
bool IsWritableExecutableMemorySupported() { return true; }

FileMappingHandle CreateFileMappingHandle(const std::filesystem::path&, size_t length, PageAccess, bool) {
  std::lock_guard lock(guard);
  Object object{};
  if (!length || !CreateObject(length, object, true)) return kFileMappingHandleInvalid;
  // Callers expect zero pages, as a new file gives them (lazy chunks are zeroed
  // when they get memory).
  const size_t eager = object.bytes - object.lazy_limit;
  if (void* whole = MapObject(object, object.lazy_limit, 0, eager)) {
    std::memset(whole, 0, eager);
    sceKernelMunmap(whole, eager);
  } else {
    ReleaseObject(object);
    return kFileMappingHandleInvalid;
  }
  objects.emplace(next_handle, object);
  return next_handle++;
}
void CloseFileMappingHandle(FileMappingHandle handle, const std::filesystem::path&) {
  std::lock_guard lock(guard);
  auto i = objects.find(handle);
  if (i == objects.end()) return;
  ReleaseObject(i->second);
  objects.erase(i);
}
void* MapFileView(FileMappingHandle handle, void* address, size_t length, PageAccess access, size_t offset) {
  std::lock_guard lock(guard);
  const auto object = objects.find(handle);
  const size_t bytes = RoundUp(length, kPage);
  const uintptr_t wanted = reinterpret_cast<uintptr_t>(address);
  if (object == objects.end() || !bytes || wanted % kPage || offset % kPage ||
      offset > object->second.bytes || bytes > object->second.bytes - offset) return nullptr;
  void* mapped = MapObject(object->second, offset, wanted, bytes);
  return mapped ? AddView(mapped, bytes, handle, false, access, offset) : nullptr;
}
bool UnmapFileView(FileMappingHandle handle, void* address, size_t length) {
  std::lock_guard lock(guard);
  auto i = views.find(reinterpret_cast<uintptr_t>(address));
  if (i == views.end() || i->second.handle != handle || i->second.size != RoundUp(length, kPage)) return false;
  if (sceKernelMunmap(address, i->second.size)) return false;
  views.erase(i);
  return true;
}
void* AllocFixed(void* address, size_t length, AllocationType type, PageAccess access) {
  std::lock_guard lock(guard);
  const uintptr_t wanted = reinterpret_cast<uintptr_t>(address);
  if (type == AllocationType::kCommit)
    return address && SetAccess(wanted, length, access, nullptr) ? address : nullptr;
  const size_t bytes = RoundUp(length, kPage);
  if (!bytes || wanted % kPage) return nullptr;
  Object object{};
  if (!CreateObject(bytes, object)) return nullptr;
  void* mapped = MapObject(object, 0, wanted, bytes);
  if (!mapped) {
    sceKernelReleaseDirectMemory(object.start, object.bytes);
    Note("alloc-fixed", wanted, bytes, -1);
    return nullptr;
  }
  std::memset(mapped, 0, bytes);
  objects.emplace(next_handle, object);
  return AddView(mapped, bytes, next_handle++, true,
                 type == AllocationType::kReserve ? PageAccess::kNoAccess : access);
}
bool DeallocFixed(void* address, size_t length, DeallocationType type) {
  std::lock_guard lock(guard);
  const uintptr_t at = reinterpret_cast<uintptr_t>(address);
  if (type == DeallocationType::kDecommit) {
    if (at % kGuestPage || !length) return false;
    const size_t bytes = RoundUp(length, kGuestPage);
    // A later commit observes zero pages.
    // The pages at the two ends may be held closed by a watch: the hold is
    // lifted while this writes the zeros (a fault here would wait for `guard`
    // for ever), and put back after.
    auto i = Containing(at, bytes);
    if (i == views.end()) return false;
    View& view = i->second;
    // Never given memory: nothing to zero.
    {
      const size_t first_guest = (at - i->first) / kGuestPage, guest_count = bytes / kGuestPage;
      bool any = false;
      for (size_t page = first_guest / kPerPage; page <= (first_guest + guest_count - 1) / kPerPage; ++page)
        any |= view.backed[page] != 0;
      if (!any) return SetAccess(at, bytes, PageAccess::kNoAccess, nullptr);
    }
    const size_t first = (at - i->first) / kPage, last = (at - i->first + bytes - 1) / kPage;
    const uint8_t first_hold = view.hold[first], last_hold = view.hold[last];
    view.hold[first] = view.hold[last] = kNoHold;
    if (!SetAccess(at, bytes, PageAccess::kReadWrite, nullptr)) return false;
    std::memset(address, 0, bytes);
    const bool good = SetAccess(at, bytes, PageAccess::kNoAccess, nullptr);
    if ((at - i->first) % kPage) view.hold[first] = first_hold;
    if ((at - i->first + bytes) % kPage) view.hold[last] = last_hold;
    Apply(i->first, view, (at - i->first) / kGuestPage, bytes / kGuestPage);
    return good;
  }
  auto i = views.find(at);
  if (type != DeallocationType::kRelease || length != 0 || i == views.end() || !i->second.owns_object) return false;
  if (sceKernelMunmap(address, i->second.size)) return false;
  const auto object = objects.find(i->second.handle);
  if (object != objects.end()) {
    sceKernelReleaseDirectMemory(object->second.start, object->second.bytes);
    objects.erase(object);
  }
  views.erase(i);
  return true;
}
bool Protect(void* address, size_t length, PageAccess access, PageAccess* previous) {
  ++xbox360ps5::protect_calls;
  std::lock_guard lock(guard);
  return SetAccess(reinterpret_cast<uintptr_t>(address), length, access, previous, true);
}
// PS5X360E: Xenia Edge's user mode views (KeCreateUserMode, used by system
// software rather than games) need a window of 64 KiB views replacing a
// reservation one at a time. Not done on the console yet: the user mode
// window cannot be claimed and KeCreateUserMode reports no memory.
bool ReserveFileViewPages(void*, size_t) { return false; }
void* MapFileViewPages(FileMappingHandle handle, void* base_address, size_t length, PageAccess access,
                       size_t file_offset) {
  return MapFileView(handle, base_address, length, access, file_offset);
}
bool ReleaseFileViewPages(FileMappingHandle handle, void* base_address, size_t length) {
  return UnmapFileView(handle, base_address, length);
}
bool QueryProtect(void* address, size_t& length, PageAccess& access) {
  std::lock_guard lock(guard);
  const uintptr_t at = reinterpret_cast<uintptr_t>(address);
  auto i = Containing(at, 1);
  if (i == views.end()) return false;
  const auto& pages = i->second.access;
  size_t first = (at - i->first) / kGuestPage, end = first + 1;
  while (end < pages.size() && pages[end] == pages[first]) ++end;
  access = static_cast<PageAccess>(pages[first]);
  length = (end - first) * kGuestPage;
  return true;
}
}
