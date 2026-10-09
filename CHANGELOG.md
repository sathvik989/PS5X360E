# Changelog

PS5X360E versions are `0.X-alpha.N+<PS5X360 base>` (0.2 and up: on Xenia Edge): an alpha number of this fork, then the
PS5X360 release it is built on.

## 0.2-alpha.31+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Found the bad fog data in NFS The Run. Of the three 256x32 float buffers the fog is computed
  into in turn, the one at 0A680000 holds very different values every time it is rendered (mean
  0.115, 7243 zeros) from the other two (mean 0.778, 1802 zeros): the dark frame. Its memory
  starts on a 16 KiB PS5 page; the other two share their last page with data the CPU writes
  next to them. The trace now also logs CPU writes to and texture reads of the 256 KiB after each
  buffer, to find which input of that frame's fog pass is stale.

## 0.2-alpha.30+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Reviewed everything PS5X360 changed from 0.5.7-fix.1 to 0.5.9 (the 0.5.8 series is only in
  0.5.9's source archive). Ported what applies to Edge: swapchain format selection (a single
  advertised format was ignored), faster and stricter package reads (GoD/STFS/SVOD seek straight
  to the block, short reads fail instead of handing the game partial data), race-free texture
  outdated flags. Also an Edge bug found in the review: the network byte count (FIONREAD) was
  returned byte-swapped, and a game asking for its next disc crashed (no file picker on the
  PS5); it now fails cleanly until multi-disc support is ported.
- Not ported: Kinect/phone camera, OpenGL renderer, Canary readback and thread-suspend fixes
  that Edge's own code already covers, diagnostics. Still to port: multi-disc switching, the
  on-screen keyboard, shader constant upload speed-ups, cache file hardening.
- The frame trace (guide: Measure performance) now records what NFS The Run's float render
  output contains each frame (min, max, mean, zeros, NaN, infinities per channel), to find the
  frame whose fog data is wrong.

## 0.2-alpha.29+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- From PS5X360 0.5.9 (BrinooTk), whose source archive only ships as overlays on 0.5.7-fix.1:
  two graphics fixes that apply to Edge as well. The gamma ramp read by in-flight frames is no
  longer rewritten under them (PS5X360's Forza Horizon brightness investigation), and textures
  sampled with explicit register gradients no longer get their horizontal and vertical
  gradients swapped. The rest of 0.5.9 is multi-disc switching, phone Kinect, MW3 socket
  fixes, cache changes Edge already covers, and diagnostics.
- NFS The Run's fog and smoke: the shader-maths test (no contraction), direct host resolves and
  the multiple render target range clamp made no difference.

## 0.2-alpha.28+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- alpha.27 fixed the 2x white-out; 2x now looks like 1x. The 1x flicker remains, still every
  third frame: in the dark frame the distant fog and sky lose their haze (greenish sky, saturated
  buildings). alpha.27's trace shows the one thing that differs between the three frames: the CPU
  writes just past the end of two of the three 128 KiB float buffers, inside the same 16 KiB PS5
  page, and each write first forced a synchronous copy of that buffer's GPU output back to guest
  RAM; the third buffer ends on a page boundary and never did.
- Render output the CPU has not read in 8 resolves in a row is no longer watched for reads
  (every 64th resolve checks again). That removes those copies, and a kernel call per resolve
  (about 115 a second in NFS). `[GPU] readback_skip_unread_resolves = false` turns it off.

## 0.2-alpha.27+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- 2x: the copy of 2x output back to 1x (for the CPU and anything reading it at 1x) skipped
  128-bit-per-pixel images. alpha.26's trace showed NFS The Run's three-buffer 256x32 float image
  is exactly that, and its copy failed every frame at 2x; the 2x video shows the sky whiting out
  every third frame and white smoke. Edge's downscale shader now handles 128-bit pixels.
- The frame trace logs every GPU request of memory those 128-bit images were written to (USE),
  to find what reads them at 1x.

## 0.2-alpha.26+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- 2x now runs NFS The Run at 26-30 FPS (was 21-24). Remaining at 2x: tyre smoke as white blobs
  with stepped edges, grey blocks under the car.
- alpha.25's trace found what changes every third frame: NFS renders a small 256x32 RGBA float
  image into one of three buffers in turn (0A660000, 0A4E6000, 0A356000) and never samples it as
  a texture, so the CPU reads it back. One of the three is aligned to a 16 KiB PS5 page, the
  other two are not. The trace now records the CPU's reads of GPU output, each copy-back to
  guest RAM (or why none was made), the mirroring of 2x output for the CPU, and output kept
  through a neighbouring write.
- The 30 s performance summary splits watch arming into watches for CPU writes and for CPU
  reads of GPU output (memory protection costs NFS 80-120 ms of CPU time a second).

## 0.2-alpha.25+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- New Video setting Render resolution: 1x (720p, original) / 2x (1440p, demanding), per game,
  applied by restarting the game. It was left out of the settings since scaled rendering ran out
  of memory; alpha.17 gave it the memory and alpha.19 fixed the 2x crash. A game config's
  draw_resolution_scale still wins over it.

## 0.2-alpha.24+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Changing Anti-aliasing during play did nothing: the picture settings only reached the screen
  when the image filter, sharpness or dither changed too. SMAA, FXAA and Show edges now apply as
  soon as they are chosen. (SMAA chosen before launching a game did run.)

## 0.2-alpha.23+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Anti-aliasing now offers Off / SMAA / SMAA + FXAA / FXAA / SMAA (show edges). FXAA runs at its
  highest quality preset after SMAA; both run on the game's own picture, before the Image filter
  (FSR upscales, then sharpens). Show edges paints the edges SMAA found red and green, to check it
  is working on a game that already smooths its edges.
- The fog flicker in NFS The Run is still there with alpha.22, so the 16 KiB page fix (kept, it
  is correct) was not its cause. "Measure performance (5 s)" in the guide now also records 9
  frames of the GPU's resolves and texture loads, to compare a dark frame with the normal ones.

## 0.2-alpha.22+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- New Video setting Anti-aliasing: Off / SMAA. SMAA smooths jagged edges on the game's picture
  before the image filter, so it combines with FSR (upscale, then its RCAS sharpening) or CAS.
  Off by default.
- Shader cache: the compiled pipelines were saved only when a game closed cleanly, which never
  happens on the PS5, so every launch compiled them all again (NFS: 433 pipelines, 3.4 s of
  compiling, hitches up to 0.35 s). They are now saved during play, 10 s after new ones appear,
  at most once a minute. The log says how many stored pipelines a launch found, and why if the
  driver gave nothing to save.
- The per-pipeline log line added for the 2x crash is gone (less log work at 2x).
- Builds reconfigure when source or shader files were added, so new shaders are compiled.

## 0.2-alpha.21+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Distant fog and smoke flickering in NFS The Run: alpha.20 showed it is not the visibility
  queries (Strict gives steady sun results and the flicker stays; Strict also no longer
  freezes). A capture of the flicker shows every third game frame with the fog darker and the
  buildings behind it magenta. Cause, PS5-only: the GPU's copy of guest memory was tracked in
  16 KiB host pages, four 4 KiB guest pages each, so a CPU write to one guest page threw away
  render output in the other three and uploaded stale guest RAM over it. Tracking is now per
  4 KiB page; output the write did not touch is kept, and checked against a hash of its guest RAM
  before the GPU uses it again. `[GPU] guest_page_gpu_output_tracking = false` turns it off.
- The log notes every 30 s how many pages that kept.

## 0.2-alpha.20+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Visibility queries set to Strict froze NFS The Run after about 80 s (the screen said Xenia
  crashed; the log shows a stall, no crash): the main thread polled a query result while the GPU
  sat idle. Edge's strict mode only forced results the game marked as pending; it now also
  resolves whatever is queued once the GPU has been idle for a moment.
- The stall report now says what the GPU thread is waiting on (guest commands, idle, query
  results, pipelines, the host GPU, a WAIT_REG_MEM) and the occlusion query counts.
- `[GPU] occlusion_query_log = true` in a game config prints one short `ZPDT` line per query
  (first 6000), to study the distant smoke and fog flicker.

## 0.2-alpha.19+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Rendering at a scale above 1 no longer crashes NFS The Run: alpha.18's log named a quad list
  pipeline (VS 927ACDEA4618196D, PS DC158E6DFE1CB854) whose geometry shader the PS5's RADV fails
  to build. When scaled, quad lists are now drawn as triangle lists by index (Edge's own
  conversion; a game config with `[GPU.Debug] force_convert_quad_lists_to_triangle_lists = true`
  did the same on alpha.18 and the game ran).
- NFS at 2x still has wrong effects (pink washes, overbright and flickering smoke, black
  squares) and runs at 21-24 FPS; 1x remains the setting for it.

## 0.2-alpha.18+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- At 2x NFS The Run crashes inside the PS5's RADV (radv_pipeline_init_vertex_input_state: a
  pipeline with no vertex-stage shader), also with background shaders off, so one particular
  pipeline rather than concurrent creation. With a draw resolution scale above 1 each pipeline
  is now logged (shader hashes, stages, geometry shader, topology) and the log flushed before the
  driver gets it: the last such line before a crash names the one.

## 0.2-alpha.17+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- About 4 GiB more memory for games and the GPU. Xenia maps the guest's whole 4 GiB address
  space plus its 512 MiB of RAM; on the PS5 every byte of that was direct memory from the start
  (the console has 12 GiB; about 6 were gone before a game ran). The 4 GiB address space now gets
  memory 1 MiB at a time when the guest first commits it; the 512 MiB of guest RAM, which the GPU
  imports whole, is still allocated up front. Checked against a model of the console's memory
  calls on a PC (commit, aliasing views, protections, decommit).
- Aimed at rendering at 2x (game config `draw_resolution_scale_x/_y = 2`): NFS The Run drew at
  2560x1440 on alpha.16 but ran out of memory ("Failed to create ... texture blit staging
  image"), with black effects and then a crash.

## 0.2-alpha.16+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Game config options that are only read as the emulator starts now take effect, such as the
  draw resolution scale (`[GPU]` `draw_resolution_scale_x/_y = 2`). A game with a config file is
  started the way games with start-time settings already were: the title restarts straight into
  it and applies the file before the GPU is set up. In alpha.15 the file was read, but only after
  the GPU had been set up at 1x.

## 0.2-alpha.15+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- The "120 Hz (test)" video clock works again: guest vblanks come at 120 Hz (new core option
  guest_vblank_rate_override; Edge itself only has 50/60). A game that shows a frame every
  second vblank, 30 FPS on the Xbox 360, can then reach 60; one that times itself by vblanks
  runs too fast instead. A per-game test, as on Canary.

## 0.2-alpha.14+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Need for Speed: The Run's recommended settings add Fast locks, Video memory optimized and the
  alternative fast visibility queries. With them races run at 27-30 FPS (the game's own 30 on
  the Xbox 360) instead of 12-20: the guide's measurement had shown its threads waiting on the
  core's global lock, as in GTA IV; the visibility mode fixed its collision sparks.

## 0.2-alpha.13+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- NFS The Run plays (races at 11-25 FPS on alpha.10 with readback on).
- The guide's "Measure performance (5 s)" samples the guest scheduler's six dispatch threads
  ("Guest CPU 0".."Guest CPU 5"), where all guest code runs since alpha.8. Before, it sampled
  only threads with a PS5 thread of their own, so under the scheduler it saw none of the game.

## 0.2-alpha.12+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Need for Speed: The Run runs past the garage with "Image readback by the game" on (user,
  0.2-alpha.10, per-game setting). It is now the recommended setting for title 4541094A, so the
  game gets it without changing anything. The launcher's general default stays off; Xenia Edge
  on PC has it on.

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
