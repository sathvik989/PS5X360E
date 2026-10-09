# Changelog

PS5X360E versions are `0.X-alpha.N+<PS5X360 base>` (0.2 and up: on Xenia Edge): an alpha number of this fork, then the
PS5X360 release it is built on.

## 0.2-alpha.11+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Per-game config files work again: `<title id>.config.toml` in `/app0/assets/game-configs` or
  `/download0/xbox360ps5/game-configs` is applied when the game starts, over the launcher's
  settings (`[Category]` then `name = value`, as Edge's game configs). The Edge build never
  read them: the loader was not part of the title and Edge has no `ReadGameConfig`. So the
  `guest_scheduler = false` switch described for alpha.8 only takes effect from this build.

## 0.2-alpha.10+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- NFS The Run stops in the same state with VSync on (tested on alpha.8): the same marker word
  (3 of 8) missing, the same eight-submission backlog. It is deterministic, not a timing race.
- The 8-word marker block is the size of the GPU's eight scratch registers, which the command
  processor can write back to memory. The stuck-wait report now prints the scratch write-back
  address, mask and registers, records scratch register writes, and decodes the packets still
  ahead of the wait in the current indirect buffer and in the primary ring (what the GPU would
  run next, and where the missing write would come from).

## 0.2-alpha.9+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fix VSync off making the emulated console's vblanks unlimited. Since 0.2-alpha.2 the VSync
  setting was mapped to Edge's guest_display_refresh_cap, whose "off" hands the game vblanks as
  fast as it takes them; on Canary, VSync off still paced them at 60 Hz (the setting's own
  description says so). Guest vblanks are now always capped at 60 Hz (50 Hz in PAL mode).
- Why it matters: with alpha.8 (guest scheduler on) NFS The Run gets further, then stops with its
  CPU side eight GPU submissions ahead of a GPU wait that the CPU has yet to satisfy: the job
  that should satisfy it waits for submission room. Unlimited vblanks let the game queue frames
  as fast as it can, which is how it gets that far ahead.
- The 120 Hz video clock option no longer changes the guest vblank rate (Edge has 50 and 60 Hz
  only); it only raises the presentation limit.

## 0.2-alpha.8+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Xenia Edge's guest scheduler is on, as it is in Edge on PC: guest threads run as fibers on
  six dispatch threads, one per Xbox 360 hardware thread, instead of one PS5 thread each (the
  Canary model the earlier builds kept). Edge's NFS The Run fixes were made with it on.
- Fiber stacks come from direct memory on the console, 4 MiB each (Edge asks for 16 MiB from
  anonymous mmap, which on the PS5 is charged to the 448 MiB flexible budget and would run out
  with NFS The Run's ~60 threads). Guest threads already ran on 2 MiB stacks before.
- To go back to one PS5 thread per guest thread for a game, put
  `[Kernel]` / `guest_scheduler = false` in
  `/download0/xbox360ps5/game-configs/<title id>.config.toml`.

## 0.2-alpha.7+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- NFS The Run now draws the garage (alpha.6's flush of GPU output before memory waits got it
  past the first stuck wait), then stops at the next one. alpha.6's report shows no GPU packet
  writes the 0xFFFFFFFF markers: the game's CPU code does, and a job thread is meanwhile waiting
  for a GPU frame fence three frames ahead, behind the stuck wait.
- More diagnostics for that: each memory wait that has to wait records the marker block as it
  found it and, when it ends, how long it took; any copy of GPU output (resolve or memory
  export read back) into guest RAM on the page of the marker block is logged, in case the
  emulator overwrites markers the game had written.

## 0.2-alpha.6+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Need for Speed The Run still stops after Start (around My Cars) exactly as it did on the
  Canary core: the same guest code spins, and the GPU waits forever at the same kind of
  WAIT_REG_MEM for a fourth 0xFFFFFFFF marker next to three that did arrive. Xenia on PC has not
  been reported past this point either.
- A memory WAIT_REG_MEM that is not met at once now first hands pending resolves to the GPU and
  waits for memory-export output the guest may be waiting for (on the console, output of earlier
  GPU work is there for the command processor to see).
- When such a wait still lasts 3 seconds, the log now lists the last 64 GPU packets and the last
  64 GPU memory writes, resolves, memory exports and interrupts, to find what should have
  written the marker. EVENT_WRITE packets with an address (not emulated) are logged once per
  event.

## 0.2-alpha.5+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fix the controller doing nothing in game (Start, and every other button): Xenia Edge only
  passes input to a guest controller slot that is bound to a device its drivers list, and the
  DualSense driver listed none, so no slot was ever bound to it. It now lists the pad (player
  1's from the start, the others while connected) and asks Edge to bind again when a pad
  connects or disconnects.

## 0.2-alpha.4+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fix the console freezing when a disc image game starts ("LOAD classify source" was the last
  step logged): Xenia Edge checks whether a disc image is an original Xbox game by mapping the
  whole image into memory, which the PS5 cannot do with a multi-gigabyte file. It now reads only
  the image's header and root directory. The same for reading an original Xbox disc's details.

## 0.2-alpha.3+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fix the crash right after "emulator setup returned": Xenia Edge's Setup no longer creates the
  graphics and audio systems or attaches the controllers (its SetupSubsystems does), so the
  launcher found no graphics system. The PS5 title now calls SetupSubsystems after Setup.
- tools/ps5x360e-symbolize.sh uses the new builder image and also prints the machine code and
  source lines at a crash in the eboot.

## 0.2-alpha.2+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fix the crash at start (before the window): two settings changed type in Xenia Edge.
  readback_resolve is a switch there (Canary: none/fast/full), so writing "none" into it crashed;
  framerate_limit is 32-bit. The launcher's readback setting now turns Edge's readback off only
  for "none". Every other setting the PS5 code touches was checked against Edge's types.

## 0.2-alpha.1+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- The emulator core is now Xenia Edge (has207/xenia-edge 2e6898afc) instead of Xenia Canary
  b083312b8: PS5X360's PS5 patch was carried over to it as a whole rather than porting Edge fixes
  one at a time. patches/canary/xbox360ps5.patch is now the patch against Xenia Edge; the separate
  patches/ps5x360e/ ports are gone (they are part of Edge), except the crash dump and the stuck
  WAIT_REG_MEM report, which are in the main patch.
- Left out for now on the PS5 (Edge reworked these parts): the "optimized video memory" unwatched
  pages, mounting Kinect content packages from game files, PS5X360's auto-reset event ticket
  wait, its xconfig and socket changes (Edge's asio sockets are used). Edge's cooperative guest
  scheduler is off on the PS5 by default; Edge's user mode views (KeCreateUserMode) are not
  supported on the console yet.
- Settings mapped to Edge: VSync is Edge's guest_display_refresh_cap, the internal resolution
  setting uses Edge's internal_display_resolution; the memexport readback setting does nothing
  (Edge always reads memexport back). The kernel's dialogs still answer themselves on the PS5
  (Canary's headless mode, which Edge removed, kept for the PS5).
- JIT code cache 512 MB on the PS5 (Edge: 1 GB; Canary's PS5 build: 256 MB); generated code may
  be placed anywhere (Edge's encoded indirection), and crash/stall reports follow it.
- Build: Slang shader compiler 2026.8 in the builder image, and Edge's xenia-shader-cc built for
  the build machine (tools/host-shader-cc). Builds in build/edge-ps5.

## 0.1-alpha.3+0.5.7-fix.1 (PS5X360E)

- Port Xenia Edge's ring read pointer fix: the GPU publishes how far it has read the command ring
  every RB_BLKSZ dwords instead of only when a whole burst is done. In NFS The Run (My Cars) the GPU
  waited in WAIT_REG_MEM partway through a burst for a value a job thread writes, while that thread
  waited for ring space the GPU had already read but not reported: a deadlock.

## 0.1-alpha.2+0.5.7-fix.1 (PS5X360E)

- GPU WAIT_REG_MEM flushes pending work (occlusion query reports, resolves) on every wait, also with
  emulated VSync off, as Xenia Edge does; only the sleep depends on VSync. A wait that never ends is
  logged once with what it waits for. In NFS The Run the GPU sat in such a wait while a job thread
  waited for the GPU.
- Add tools/ps5x360e-symbolize.sh to turn eboot+0x... places in PS5 logs into function names.

## 0.1-alpha.1+0.5.7-fix.1 (PS5X360E)

- Port Xenia Edge's lwarx/stwcx. reservation rework (generation counters per 128-byte granule,
  host atomics on the same reservations, isync acquire barrier) and its 32-bit address wrap fix.
  Targets the Need for Speed: The Run hang and crash in My Cars (job threads spinning in CAS loops).
- Crash and stall reports print the host registers and code at a fault and the guest code that
  stalled threads are in.
- Show PS5X360E and its version on screen and in game logs.

## 0.5.7-fix.1

- Resolve the reported Left 4 Dead 2 loading stall on the development PS5.
- Balance nested guest/host thread suspension counts and protect startup transitions.
- Sign extra players in only while their controllers are connected.
- Add confirmed profile removal with account/save backups in saves/removed-profiles.
- Distinguish stored accounts from signed-in players in logs.
- Keep AutoLog 1.0.9 unchanged. See docs/releases/v0.5.7-fix.1.md for validation and installation.

## 0.5.7

- Fix Left 4 Dead 2 (start, menu text, match loading) and Forza Horizon closing at START (fiber switches).
- Fix readbacks returned before the GPU had finished, guest socket addresses on the PS5, cut crash records
  and Arcade/LIVE packages started as games; add nested packages and the LZX decompression service.
- Make the v0.5.6 speed-ups (fast locks, optimized video memory) options that are off by default.
- Add per-game settings, the game sheet (Play, Patches, Settings, Achievements) and new video, performance,
  audio and control options.
- Add the settings page for a phone or computer, with log download as ZIP.
- Keep the library up to date by itself, announce cover downloads, reload the list with OPTIONS.
- Add four local players, achievement notifications and lists, and interface sound volume.
- Report what a stopped game was waiting for in its log.
- Add the camera motion research option (off by default; not Kinect support).
- See `docs/releases/v0.5.7.md` for installation, testing limits and what was seen on a console.

## 0.5.6

- Improve shared memory lookups, invalidation granularity and CPU/GPU synchronization.
- Add adaptive dynamic vertex-buffer uploads with a Guide toggle.
- Improve game compatibility through shared memory, thread-lifetime and storage fixes.
- Add the Saved data panel, homebrew save storage and visible compiled build version.
- Include optional AutoLog 1.0.9 with better guest-crash classification and delivery diagnostics.
- Disable automatic intrusive CPU sampling in public builds; preserve manual measurement.
- See `docs/releases/v0.5.6.md` for installation and testing limits.

## 0.5.5-preview (local test build)

- Display the compiled version throughout the launcher and in the in-game Guide.
- Add explicit guest-kernel crash markers and guest context to per-game logs.
- Bound diagnostic address arithmetic at the guest's 4 GiB address limit.
- AutoLog 1.0.9-preview preserves the guest-kernel classification when register
  dumps are also present. These are diagnostics, not a universal crash fix.

## 0.5.4-preview (local test build)

- Fix final-owner thread-exit ordering on POSIX to prevent use-after-free of
  native thread mutexes after a guest closes its thread handle.
- Store saved content, profiles and achievements in the homebrew installation's
  `saves` folder, with an initial preserving copy and legacy fallback.
- Add Settings > Saved data in English, Portuguese and Spanish.
- AutoLog 1.0.8-preview classifies Guide returns as normal session completion.
- Host memory and migration regressions pass; console gameplay remains unverified.

## 0.5.3-preview

- Refined the game-library UI, cover-flow presentation, navigation and settings.
- Improved support for Xbox 360 ISO disc images, alongside extracted XEX games
  and GOD/STFS packages, with safer file reading and executable loading.
- Added compatibility fixes across multiple games, including loading, memory
  handling and version-matched patch selection. Compatibility still varies by title.
- Keep automatic guest video pacing at 60 Hz when emulated VSync is disabled,
  including the per-title patch override, to prevent accidental acceleration.
- Update VSync help in English, Portuguese and Spanish.
- Publish optional PS5-only AutoLog 1.0.5-preview with session-event reporting,
  a first-activation cutoff, durable retry queue and a reporting opt-out.

## 0.5.2-preview -- development build

- Open disc images and read-only packages without mapping them on the console: directory tables,
  headers and hash tables are read from the file as well as file data.
- Load executables from an unmapped disc image through the file reader.

## 0.5.1-preview -- development build

- Read disc images (ISO) and packages (STFS, GOD/SVOD) from the file with `pread` on the console
  instead of copying out of a whole-file mapping.
- Catch only `std::exception` at the kernel-call boundary (a catch-all took the forced unwinding
  that ends a thread on glibc hosts).
- Add `tools/make-test-iso.py`, which packs an extracted game folder into a disc image for tests.

## 0.5.0-preview -- development build

- Keep regenerable data (shader and module caches, guest `cache:` partitions) in the
  installation folder when it is writable; the save storage image keeps saves and profiles.
- Treat the instruction info cache as optional instead of aborting when its folder cannot be made.
- Return an unsuccessful status when a kernel call throws a C++ exception; report a launch that
  throws in the library after a clean restart.
- Validate the emulated console settings file and save it through a temporary file.
- Do not pin guest threads to host cores; hold whole-page write watches against neighbouring commits.
- Add a performance measurement to the guide and memory-watch counters to the periodic log line.


## 0.4.9-preview -- development build



- Detect ISO signatures without redundant full-disc mappings and directory walks.

- Close signature probes on every path and check file metadata/read failures.

- Correct offset-to-EOF mapping lengths and report file mapping errors.

- Persist loading phases without enabling verbose logging.

- Remove mutex-protected network logging and drain waits from signal reports.



## 0.4.8-preview -- development build



- Resolve relative module subdirectories and query loaded module names first.

- Preserve disk-full status and existing save headers on failed writes.

- Count successful renderer-output refreshes separately from swap requests.

- Record build identity and memory-reservation errors for diagnostics.


## 0.4.7-preview -- development build

- Merge duplicate patches after matching the executable version.
- Block conflicting patch writes; activating a conflict in the menu disables the old choice.
- Honor explicit disabled-VSync patch requirements per title without changing saved preferences.
- Refresh native vblank pacing after a patch override.

## 0.4.6-preview -- development build

- Avoid fatal exceptions when persisting profile/content headers during startup.
- Write complete padded metadata through a temporary file; retain old data on failure.
- Keep early startup and fatal-signal reports in installation logs/boot.log when writable.


## 0.4.5-preview -- development build

- Connect launcher patch choices to Canary before guest execution and JIT precompilation.
- Reuse Canary executable hashes for version matching and library metadata.
- Validate memory ranges, verify writes, reject incompatible versions and log applied names.
- Accept Windows line endings in patch choices; report VSync requirements.

## 0.4.5-preview — development build

- Connect launcher patch choices to Canary before guest execution and JIT precompilation.
- Reuse Canary executable hashes for version matching and library metadata.
- Validate patch memory ranges, verify writes, reject incompatible versions, and log applied names.
- Accept Windows line endings in patch choice files; report VSync requirements.

## 0.4.4-preview — development build

- Reduce normal logging to session identification, warnings and errors; disable
  duplicate platform stdout output and hidden debug-file activation.
- One-time reset of detailed logging, with subsequent explicit choices saved.

## 0.4.3-preview — development build

- Restore writable guest `cache:`, `cache0:` and `cache1:` devices in both
  native and host startup. This frontend setup was absent despite desktop
  Canary mounting the same devices. GTA IV logs showed missing cache paths.
- Keep utility cache separate from shader caches, games and saves. A VFS
  regression verifies guest writes, distinct devices and persistence.

## 0.4.2-preview — development build

- Prefer the installation's lowercase `logs` folder for per-game sessions.
- Keep sandbox logging when the title mount is not writable; the desktop helper
  exports those sessions to `/data/homebrew/PPSA50011/logs` and downloads logs
  already exported there even when the title is closed.

## 0.4.1-preview — development build

- Library duplicate detection uses the complete normalized source path without
  relying on native realpath. Different folders containing `default.xex` remain
  separate games; overlapping configured folders still deduplicate.
- Scan logs record every discovered game and directory enumeration errors.

## 0.4.0-preview — development build

- Saved VSync control in the library settings. Changing it restarts the native
  title when the panel closes, before Canary initializes its frame limiter.
- Cover layout follows the supplied Aurora reference: a larger parallel center
  cover, overlapping neighbours receding in size and a floor reflection.
- Regression checks cover VSync persistence/restart and cover overlap/alignment.

## 0.3.0-preview — development build

- Automatic log files per game launch, with game/source identification, UTC
  timestamps, build version and native crash-report routing.
- A preserved first segment and three rotating segments for verbose sessions.
- Configurable game folders with a controller-operated folder picker; offline
  locations persist and overlapping roots do not duplicate games.
- `console.py game-logs` retrieves mounted log files through FTP and packages
  them in a ZIP ready to share.

## 0.2.0-preview — development build

- Projected 3D cases with visible spines and tops, continuous rotation and
  subdivided cover textures for perspective.
- Frame-progress diagnostics now include loading that never submits a frame.
  Captures are bounded and reset when rendering resumes.
- A translated no-frame notice preserves access to the touchpad guide.
- Garden Warfare online-service limitations are documented. This build does
  not implement Xbox Live/EA authentication or claim to fix its loading.

See the [development notes](docs/releases/v0.2.0-preview.md).

## 0.1.0-preview — 2026-10-03

First public preview: Xenia Canary PS5 port, native audio and DualSense input,
RADV Vulkan rendering, game library, profiles, community patches, covers and
the touchpad guide. Includes Noto Sans fonts and English, Portuguese and Spanish
UI translations with automatic console-language selection.

GPU upload and thread-stack changes improved Sonic and Budokai host reproductions.
PS5 gameplay validation and compatibility refinement are ongoing; Burst Limit
and Ultimate Tenkaichi fight black screens remain unresolved.

See the [release notes](docs/releases/v0.1.0-preview.md) for verification limits.
