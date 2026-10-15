# PS5X360E to-do

Last updated 2026-10-15, during alpha.98. The newest state is at the top; finished items move to the CHANGELOG.

## Now: alpha.98 (built; waiting for the user's test)

- [x] **Object lookups without the global lock (test)** (`handle_lookups` → `object_table_shared_lookups`):
  the object table gets its own spinning reader-writer lock (`ObjectTableLock`). Lookups take it
  shared; changes to the table take the global lock and then this lock.
- [x] **Save without stopping the game (test)** (`save_delete_background` → `content_delete_in_background`
  and `content_off_global_lock`): the save's folder is moved aside and deleted on a thread of its
  own, and save calls hold `ContentManager::content_lock_` instead of the global lock.
- [x] **Exit**: the boot log records each step of closing a game (`WatchExit`). If the title is still
  there after 15 s, the main thread's stack is written every 15 s for 5 minutes. A restart now
  flushes the log (`FlushLog`) instead of closing it (`ShutdownLogging`).
- [x] Built (`ninja -n`: no work to do), symbol check clean, both trees committed, patch current,
  pushed. Next: the user tests it.
- [ ] Reply to the user with the alpha.97 findings, the settings to use and the review of the
  co-design document (sections below).

## Settings for the alpha.98 test (NFS The Run)

Turn on:
- Prepare image conversions at start (test)
- Guest clock without lock (test)
- Save without stopping the game (test)
- Object lookups without the global lock (test)
- Frame generation fixed at 2x instead of Automatic
- Diagnostics, for the logs

What to watch:
- Pauses in the menu while the game saves.
- Going back to the launcher. If it hangs, check the `BOOT exit` lines in boot.log.
- The "Performance: global lock" line: LookupObject should drop out of it.

## Findings from the alpha.97 logs (NFS The Run, October 15)

- **Menu "almost hangs"**: deleting the PROF_SAVE folder (2 entries) took 0.3–4 s under the global lock.
  All six guest CPUs stopped meanwhile: XNotifyGetNext, NtSetEvent, NtClearEvent, NtWaitForSingleObjectEx
  and KeWaitForSingleObject each held their CPU about 4 s, and the GPU thread had a 4 s frame.
- **Render target pipelines**: 12 were created on the GPU thread in 2.9 s (rt_warm_up was off).
- **Pacing**: Automatic frame generation flipped 2x↔3x when the game dipped. Some game swaps lasted
  1.5 or 2.5 refreshes, and up to 50 of 600 source frames were skipped.
- **Exit hang**: the last restart stopped right after "BOOT restarting the title". The save thread was
  inside `remove_all` (unlinkat/closedir) at that point, and the game log was missing its last lines,
  so `ShutdownLogging` is the suspect.
- **Switch threads when the IRQL drops**: 0 deferrals in NFS, so no effect there.
- **PAUSE**: 19.3 ns, about 68 cycles at 3.5 GHz (not about 140).
- **History**: alpha.86 tried save file work off the global lock and was reverted in alpha.87 as "worse".
  The ~960 ms folder creation and header write blamed on the helper thread then also show up under the
  lock (alpha.92), so they come from the storage, not the thread.

## Next, in order

1. Check alpha.98 against the user's logs: the global lock line, the save pauses, the exit steps.
2. `TextureCache::IsRangeScaledResolved` (texture_cache.cc:1451) runs 186k–295k/s under the global
   lock: make its reads lock-free.
3. Fault and protection costs under the global lock: mmio_handler.cc:454 holds about 40 ms/s, and
   `PhysicalHeap::EnableAccessCallbacks` 42–47 ms/s.
4. Run DllMain detach outside the global lock (a module unload takes 1.7 s).
5. If saves still pause: move the save's file work to an I/O worker, so the save thread's emulated CPU
   keeps running its other threads. Compare against alpha.86's result.
6. Phase E part 2: image work at 3x. Per second of GPU time: rendered textures about 270 ms, render
   target copies about 190 ms, resolves 80–160 ms.
7. Phase D: per-draw cost (VK_EXT_descriptor_buffer).
8. Driver experiment: Wave32 against Wave64 in ps5-vulkan (RADV_PERFTEST pswave32/cswave32).

## Co-design document (October 15): where each idea stands

- **Done already**:
  - Zero-copy guest memory (Zero copy option).
  - Persistent pipeline cache. Render target pipelines are kept only with rt_warm_up.
- **No gain**:
  - Hardcoding device limits (queried once at start).
  - Removing the other backends' code (not on the hot path).
- **Not feasible or wrong**:
  - eDRAM in LDS: LDS is 64 KB per CU, the eDRAM is 10 MB.
  - Skipping layout transitions: unsafe with DCC/HTILE compression.
  - A title submitting straight to the hardware rings: the driver already submits through AGC.
- **Worth trying**:
  - The Wave32/Wave64 experiment.
  - Descriptor buffers (Phase D).

## Working rules

- New behaviour comes as an option, off by default. Revert at once if a build is worse.
- UI names in English. Measure, then change.
- Version format: `0.2-alpha.NN+0.6.0`.
- Commit and push at each milestone.
- Never use the official PS5 SDK.
- Ask before reading files on the NAS when the timing is unclear. The build's `llvm-pie.elf` may be
  read for symbolizing.
- No `git reset --hard`.

## Resuming the build

- Core tree: `/home/claude/ew2`, branch `alpha81-core`, linked as `.deps/xenia-canary`. Upstream base:
  has207/xenia-edge `8e44485d9`.
- Build: `ninja -C build/edge-ps5 -k 0 -j2 xbox360ps5-title-objects` plus the core libraries (see
  `/tmp/claude-0/ps5b.sh` while the session lasts). Repeat in passes of under 10 minutes until it ends
  with exit 0 and `ninja -n` has nothing to do.
- Symbol check: every undefined symbol of the objects and archives must be defined by them or by the
  system stubs. Only `__real_thrd_create` and `wsi_videoout_set_flip_rate` may remain.
- Patch: `git diff 8e44485d9 HEAD > patches/canary/xbox360ps5.patch` in the core tree.
