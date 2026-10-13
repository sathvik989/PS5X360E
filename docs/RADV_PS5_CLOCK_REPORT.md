# Report for PS5_Mesa: clock reads and sleeps in the PS5 winsys

From PS5X360E (Xenia Edge on PS5), alpha.54, NFS The Run, against
PS5_Mesa 47e9eb61ee7e1112194557e1e047178562827bc6. Sampled with the emulator's
own profiler on the thread that records and submits Vulkan work.

## What the samples show

| Where (GPU command thread) | Share |
| --- | --- |
| `os_time_get_nano` → `timespec_get` → `clock_gettime` | ~5% |
| `radv_ps5_sleep_us` (from `radv_ps5_queue_wait_seq`, `radv_ps5_sync` waits) | ~6% |

On the PS5, `clock_gettime(CLOCK_MONOTONIC)` is a system call of about 1 µs
(what the emulator's own timing showed before it moved to the TSC). Linux answers it from the vDSO in tens of
nanoseconds, so code written for Linux treats it as free.

## Where it is called

- `radv_ps5_winsys.c`, `radv_ps5_queue_wait_seq`: reads the clock on every
  pass of the spin loop (one `pause` per pass) for up to `RADV_PS5_SPIN_NS`
  (1.5 ms). Each pass then costs about 1 µs of kernel time instead of a few
  nanoseconds, so the loop notices completion late and burns the time in the
  kernel.
- `radv_ps5_platform.c`, `radv_ps5_submit`: three reads per submission for the
  10 s submit statistics, plus two more per kick in `radv_ps5_kick_body`.
- `radv_ps5_cs.c`: two to three reads per submission (`claim_start`,
  `copy_start`, `copy_end`, `radv_ps5_note_submission`).
- `radv_ps5_sync.c`: one per pass of the fence wait loop.

## Suggestions

1. **A cheap clock on PS5.** Base `os_time_get_nano` (or a `radv_ps5_now_ns`
   used by the winsys) on `sceKernelReadTsc()` scaled by
   `sceKernelGetTscFrequency()`, taking the offset from one `clock_gettime`
   at start. PS5X360E has done this for the emulator's host clock since
   alpha.52 (`xenia/base/clock_posix.cc`). The TSC is invariant on the PS5's
   Zen 2.
2. **Fewer clock reads while spinning.** In `radv_ps5_queue_wait_seq`, read
   the clock once every 64 or so `pause`s rather than on every pass.
3. **A finer sleep after the spin.** Once the spin ends, the loop sleeps
   1000 µs per pass, so a wait that runs just past 1.5 ms ends up to 1 ms
   late. A shorter step (100–200 µs, as the `any` path in
   `radv_ps5_sync.c` already uses) or an AGC/event wait would cut that.
4. **Submit statistics behind a switch.** The timing in `radv_ps5_submit` and
   `radv_ps5_note_submission` could be enabled only when its 10 s report is
   wanted (for example `RADV_PS5_SUBMIT_STATS=1`).

With suggestion 1 alone, points 2 and 4 matter much less.
