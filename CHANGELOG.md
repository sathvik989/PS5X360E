# Changelog

PS5X360E versions are `0.X-alpha.N+<PS5X360 base>` (0.2 and up: on Xenia Edge): an alpha number of this fork, then the
PS5X360 release it is built on.

## 0.2-alpha.57+0.6.0 (PS5X360E, on Xenia Edge)

alpha.56 holds 30 FPS in NFS The Run's heaviest section (graphics thread 55-61 % busy). This
starts on what is left: the cost of watching guest memory, the loading stalls, and the profile
the next steps of the graphics thread work need.

- **Memory the game keeps rewriting is compared, not watched** ("Optimize dynamic buffers",
  on; it did nothing since the move to Xenia Edge): watching a page means protecting it, and a
  game write to it faults and unprotects it; on the console each protection change takes about
  40 µs (NFS: 900-1300 faults and 2200-3200 protection changes a second while racing, 90-140 ms
  a second). Pages that fault again and again are left unprotected; a draw that reads vertex
  data from them compares a hash of the bytes with the last upload (once per batch of commands)
  and uploads only what changed. Textures, index buffers and GPU output stay watched. Brought
  over from PS5X360's Canary build.
- **"Memory update" works again** (16 KiB): a game write makes the GPU copy re-read 16 KiB
  around it instead of up to 256 KiB, so less is uploaded and protected again.
- **No global lock for every draw**: the graphics thread took the emulator's global lock around
  each draw's vertex data, which the game's write faults hold through their 40 µs protection
  changes, so each waited on the other. Data that is already current needs no lock.
- **"Hitch:" lines**: a frame of the graphics thread over 50 ms is logged with what the thread
  did in it (shader translation, pipelines, texture loads, uploads, waits, page protections,
  write faults), to tell loading stalls apart (at most one a second).
- **"Memory shared with the GPU"** (off, experimental): the GPU reads the game's memory in
  place (Xenia Edge's zero-copy mode, made for Apple's shared-memory chips; the PS5 has one
  memory too), with no uploads. For comparing the same race on and off.
- The log now sums the unwatched pages every 900 frames ("Performance unwatched pages").

## 0.2-alpha.56+0.6.0 (PS5X360E, on Xenia Edge)

From alpha.55's measurement at NFS The Run's heaviest spot (139,000 draws a second): 29.8-29.9
FPS where alpha.53 managed 27.9, with the graphics thread 62 % busy (4.4 µs a draw) with
background submission and 73 % (5.2 µs) without it. Every 30 s window still read 29.8,
minimum 29, though nothing was busy enough to explain it:

- **The game's clock runs at the right speed again**: converting the console's 1 GHz clock to
  the Xbox 360's 50 MHz one dropped the leftover fraction of a tick (up to 19 ns) at every
  reading. Since alpha.52 made the clock cheap to read, the game reads it a few hundred thousand
  times a second while racing, and its clock (with the vertical blank it is paced by) ran
  0.4 % slow: 16.75 ms a refresh instead of 16.68 (alpha.51: 16.70). At 29.85 game frames a
  second against a 29.97 display, a frame was shown three refreshes about every 8 s. The
  leftover is now carried over.

## 0.2-alpha.55+0.6.0 (PS5X360E, on Xenia Edge)

From alpha.54's measurement (named from the build's symbols): the graphics thread still sets the
frame rate when NFS The Run has several cars and road objects on screen.

- **The submit thread gets a core of its own**: it inherited the graphics thread's placement
  ("Graphics thread on its own core", on for NFS), so the two shared one CPU, and every wait of
  the graphics thread for a submission (fence waits 119-135 ms a second, waits for data the GPU
  writes for the game 94-108 ms a second) was a wait for its own core. It now runs on the
  other cores.
- **"Background submission" setting** (on): turns the submit thread off, for an A/B comparison
  at the same spot. The log says which is used.
- **Shader constants written in bulk**: register writes were the graphics thread's largest cost
  (about 15 % with related code; alpha.54's "shader lookup" was this code, misnamed). Ranges of
  float constants, most of what a draw sets, are now copied in one pass instead of one call per
  register.
- **Memory waits looked at every 0.1 ms** (`wait_reg_mem_memory_poll_us`, 100): a GPU wait for
  memory a game CPU writes slept the 1 ms the game asks for (8.4 % of the graphics thread),
  so the work behind it started up to a millisecond late. Vertical blank waits keep their
  duration.
- `docs/RADV_PS5_CLOCK_REPORT.md`: for PS5_Mesa, the driver's clock reads (a 1 µs system
  call on the PS5, about 5 % of the graphics thread) and its 1 ms sleeps while waiting.

## 0.2-alpha.54+0.6.0 (PS5X360E, on Xenia Edge)

From alpha.53's measurement in NFS The Run's heaviest scene (25-28 FPS):

- **GPU waits are short again**: alpha.52's submit thread made each wait of the graphics thread
  for data the GPU writes for the game ten times longer (57 a second, 94 ms a second against
  9 before): the thread handed the work over, then waited for the submit thread to wake up and
  replay everything queued before it. A thread waiting for a submission now records and submits
  the queued ones itself when the submit thread isn't in the middle of one.
- **Shader lookups cached**: the graphics thread's biggest single cost (9.6 % of its time, named
  from the build's symbols) was finding the shader for each draw in a hash map of over 3000,
  every lookup a chain of cache misses. A 1024-entry table indexed by the shader's hash now
  answers repeated lookups at once.
- What the game waits on: guest CPU 3's hot function (82DDC4F8, 55-63 % of that CPU) is a spin
  helper: 32 `db16cyc` delays (the code's own back-off), then a check of the D3D device's
  state. The game is waiting, not working: on the graphics side, which these changes speed up.

## 0.2-alpha.53+0.6.0 (PS5X360E, on Xenia Edge)

- **"Measure performance (5 s)" shows the hot guest code**: alpha.52's measurement in NFS The
  Run's heavy scenes found guest CPU 3 84 % in the game's own code, 55-59 % of it in one
  function (82DDC4F8), at 28 and at 30 FPS alike. The measurement now also counts guest
  instruction addresses ("guest-pc") and writes the code of the two hottest functions of each
  busy guest CPU ("MEASURE code"), to tell a busy-wait (the game polling something the GPU or
  another thread sets) from real work, which need different fixes.

## 0.2-alpha.52+0.6.0 (PS5X360E, on Xenia Edge)

From alpha.51's four NFS The Run sessions (120 Hz and Even 30, graphics core shared and own):

- **Submissions on their own thread** (`vulkan_async_submission`, on): the graphics thread hands
  each finished batch of GPU work to a second thread, which replays it into the driver and
  submits it, and goes on with the next draws. The draw timing showed about 3.4 µs of the
  graphics thread's 6.2-7 µs per draw inside the draw itself; much of the rest is that replay
  and the driver's submission (RADV: about 0.45 ms per submission, 80-175 ms a second). Frame
  ends, waits for GPU results and copies for the CPU still wait for the second thread where
  queue order matters.
- **Cheaper clock**: the PS5's clock_gettime is a system call of about a microsecond (each
  timestamp of the draw timing cost that much). The emulator's host clock now reads the
  processor's time-stamp counter (sceKernelReadTsc), scaled to nanoseconds; the guest
  scheduler, timers and the game's own time queries all use it.
- **NFS The Run: graphics thread on its own core by default**: 5-8 % less CPU per draw in both
  pairs of runs and higher lows (Even 30: lowest 26 against 24 FPS).
- Even 30 FPS with the hardware frame rate worked: one present per frame, every frame shown
  for exactly two refreshes (883 of 883), and about half the driver time of the 120 Hz runs.

## 0.2-alpha.51+0.6.0 (PS5X360E, on Xenia Edge)

From alpha.50's two NFS The Run sessions (graphics thread shared, then on its own core):

- **Fewer driver submissions for render output the game reads**: with "Image read by the game:
  Fast", every copy of render output for the CPU was handed to the GPU at once, about 180
  times a second. RADV's own figures (now in the log) put each submission at about 0.45 ms of
  CPU (0.17 ms in the Agc submit, 0.26 ms suspended). Those copies now go at most every 8 ms
  (`readback_async_submit_spacing_ms`); one asked for sooner rides with the next copy, guest
  sync point or frame. The game already accepted the previous frame's output for these reads.
- **Draw timing now reported**: alpha.49/50 timed the draws but discarded every sample (the
  last mark was skipped), so no "per draw" figures appeared.
- **Guest CPU use measured by each guest CPU itself**: reading the six dispatch threads' CPU
  clocks from another thread gave the same figure for all six; each now samples its own clock
  every quarter second. The whole-emulator figure comes from getrusage (the process CPU clock
  read as exactly one CPU).
- Graphics thread on its own core: about 4 % less CPU per draw in the same scenes (6.0 against
  6.3 µs), within the scenes' variation; the frame rate was the same. Stays a setting, off.

## 0.2-alpha.50+0.6.0 (PS5X360E, on Xenia Edge)

- **Fixes the hang after Press Start in alpha.49.** It was not a hang: loading crawled. On the
  PS5 the runtime's unwinder refused the unwind information the JIT gives it for every compiled
  function and printed a line to standard error each time. Until alpha.48 those lines went
  nowhere; alpha.49 sent standard error to a file, so loading NFS The Run's main module wrote
  tens of thousands of lines one by one (15,519 before the stall report). The JIT no longer
  registers that information on the PS5 (it was refused anyway, and nothing there unwinds
  through generated code), standard error is buffered, and such lines are counted, not logged.
- Driver messages now work: RADV's submission breakdown appears every 10 s as
  "Driver: radv/ps5 submissions: ...".

## 0.2-alpha.49+0.6.0 (PS5X360E, on Xenia Edge)

Measurements for the graphics thread, which limits NFS The Run in heavy scenes (alpha.48's
log: about 6 µs of CPU per draw, 83 % of one core at 135,000 draws a second, while the GPU
was 25 % busy), and the first changes aimed at it.

- **Accurate visibility queries wait less**: a query the game will poll for (it marks the report
  pending, or the emulator has had to wait for that report before) is handed to the GPU as soon
  as its report is queued, instead of when the game starts polling. The GPU works on it while
  the graphics thread goes on, so the result is often there already. Same results as before; one
  more submission per such report. `occlusion_query_strict_early_submit` turns it off.
- **Graphics thread on its own core** (Settings > Performance, off by default): the GPU command
  thread stays on CPU 2, and no other emulator thread uses CPU 2 or 3. For comparing the same
  race on and off; the log names the choice ("graphics thread core dedicated/shared").
- **Draw timing**: every 64th draw is timed in eight stages (shaders, render targets, pipeline,
  textures, state, bindings, memory, render pass and draw), reported each 30 s as "per draw
  N us in IssueDraw (...)" beside the draw count.
- **CPU use per guest CPU**: a new line each 30 s gives the six emulated Xbox 360 CPUs (the
  threads running the game's code) and the whole emulator, in percent of one PS5 CPU.
- **Driver messages**: the console refused the pipe alpha.48 put on standard error, so RADV's
  output now goes to `logs/driver-messages.txt` and its lines are copied into the game log
  ("Driver: ...").

## 0.2-alpha.48+0.6.0 (PS5X360E, on Xenia Edge)

- **Xenia Edge updated** from 2e6898afc to 8e44485d9 (65 commits, 12 Oct 2026). Among them:
  pipelines are no longer waited for before a submission ends, and a draw waiting for one moves
  it to the front of the queue; occlusion query draws may use a placeholder pipeline; faster
  shader translation and pipeline creation; the storage flush that never ran (EndSubmission
  was not called) now runs; MSAA rasterizes at the Xenos sample positions; Edge's crypto is
  now Mbed TLS (the aes_128 library is gone). Our patch was moved onto it (8 conflicts
  resolved: the Kinect identity callback and the NUI dialogs keep the motion-research
  handling, and the dirty-disc dialog still returns to the launcher when headless).
- **Pipeline storage is written again.** The PS5 never grew the pipeline storage file (a whole
  NFS session logged 1367 "Failed to write pipeline description"): the file was last read to
  its final entry, and C requires a seek before writing after that. Each storage file is now
  positioned at its end before writing, and a failed write or flush is logged once with the
  reason. Pipelines compiled in one session are created at the start of the next one instead
  of stuttering again during play.
- **Even 30 FPS with the display's own frame rate**: when "Frame pacing: Even 30 FPS" is on,
  the PS5's video output holds each picture for two refreshes (VideoOut flip rate, new in
  PS5_Mesa 47e9eb6) instead of the emulator presenting it twice: half the presents, the
  timing is the hardware's. A driver without it falls back to presenting twice, as before.
- **Driver messages in the game log**: RADV's own output (its queue-submission time breakdown,
  every 10 s, among others) is logged as "Driver: ..." instead of being lost.
- **More performance figures** in the 30 s summary: draws per second and the command
  processor thread's CPU use.
- **PS5 dependencies updated** to PS5_Vulkan 252bf91: PS5_Mesa 47e9eb6 (the flip rate; 119.88 Hz
  offered to an app that declares it), PayloadSDK b83202be (the same SDK under its rewritten
  history), localeconv and strcasestr/mkstemp/readlink bound to working versions at link time.
  The pins now live in `tools/ps5x360e-deps.sh` in the fork: **download the new
  `tools/ps5x360e-build.sh` once**; after that, pin changes need no new copy. The first build
  rebuilds RADV (the slow step).
- Mbed TLS: one library (Edge's submodule, 3.6.7) built with the phone camera's configuration
  serves both Edge's crypto and the camera's HTTPS server; the separate download is gone.

## 0.2-alpha.47+0.6.0 (PS5X360E, on Xenia Edge)

- **Frame generation target 40 FPS (120 Hz)**: locked to 39.96 FPS on the PS5's 119.88 Hz output,
  each picture shown for exactly three refreshes (25 ms), the game anywhere from 10 to 44 FPS.
  Choosing it asks for the 120 Hz output by itself. Needs a 120 Hz TV; without one the output
  stays at 59.94 Hz and the lock holds 30 instead (never an uneven 40).
- The app's param.json declares high-frame-rate output (attribute3 0x80040), without which the
  PS5 refuses its 119.88 Hz mode (PS5_Vulkan's R92 finding). It only makes 120 Hz available:
  the app stays at 59.94 Hz unless frame generation asks for 120 Hz.
- Simulated at 119.88 Hz: game at 30, 24 and 20 FPS (2 and 3 vblank frames) gives 39.97
  pictures a second, three refreshes each.

## 0.2-alpha.46+0.6.0 (PS5X360E, on Xenia Edge)

- **Frame generation target 30 FPS is now a lock**: the output is a fixed 29.97 FPS grid, each
  picture shown for exactly two refreshes, with the game anywhere from 10 to 34 FPS. At a steady
  30 the game's own frames are shown (nothing generated, no optical flow cost); when it dips the
  missing ones are generated at their exact time, measured on the game's real frame times (a
  game whose frames take 2 and 3 vblanks in turn still moves evenly). Frames that arrive in a
  burst are skipped on the same clock instead of restarting it. The emulated console's vblank
  runs at 59.94 Hz in this mode, so a 30 FPS game matches the output exactly.
- Simulated: 20 / 22 / 25 / 28 / 30 FPS, steady or with 15 % uneven frame times, and games
  alternating 2 and 3 vblank frames (20 to 30 FPS): 29.9 to 30.0 outputs a second, no restarts.

## 0.2-alpha.45+0.6.0 (PS5X360E, on Xenia Edge)

- **Frame generation target** (Settings > Performance, under Frame generation): "Display rate"
  as before, or **30 FPS**: a game running between 10 and 27 FPS (NFS's 20-25 stretches) is
  filled up to an even 30 FPS, each picture shown for exactly two 60 Hz refreshes, instead of
  up to 60. Above 27 FPS the game's own frames are shown. Saved in framegen.conf as
  `ps5_frame_target=30`; a restart applies it. Simulated: 20, 22 and 25 FPS come out at 30.0,
  29.9 and 29.8 pictures a second, two refreshes each.

## 0.2-alpha.44+0.6.0 (PS5X360E, on Xenia Edge)

- **Its own app: PPSA50360, named PS5X360E.** It installs beside PS5X360 (PPSA50011) and runs
  alongside it. Settings, saves, shader caches and logs are its own (`/data/homebrew/PPSA50360`,
  `/download0/ps5x360e`); nothing of PS5X360's is read or changed.
- Games are shared: `/data/homebrew/xbox360` and PS5X360's `assets/roms` are in the default
  game list, so the same game copy works in both for comparing.
- The build now produces the whole app folder, `dist/PPSA50360` (eboot, sce_sys, the runtime
  libc, fonts, sounds, Kinect page, presets, PS5X360 0.6.0's game patch files, licenses):
  copy it to `/data/homebrew/`. The images are PS5X360's until PS5X360E has its own.
- Names shown on screen, in notifications, the phone pages and log downloads say PS5X360E.

## 0.2-alpha.43+0.6.0 (PS5X360E, on Xenia Edge)

Brought up to PS5X360 0.6.0 (and the 0.5.8 / 0.5.9 launcher work it builds on), on Xenia Edge.

- **Frame generation** (Settings > Performance > Frame generation: Off, Automatic, 2x, 3x, 4x; off
  by default): Swordpdf's ps5-framegen (FSR3 optical flow, GPL-3.0), with PS5X360's adaptive
  pacing for uneven frame rates. It adds pictures between the game's frames; it does not make the
  game run faster. SMAA / FXAA run on the generated pictures too. With frame generation on, the
  "Even 30 FPS" frame pacing steps aside (frame generation paces its own presents). The output
  rate row (120 Hz) is untested, as in PS5X360. A restart applies a change.
- **Title updates**: the game sheet's Updates tab (install, turn on, turn off; one active at a
  time; an update for another edition is flagged). Copy the package to `PPSA50011/updates` or an
  `updates` folder beside the game; Play installs the newest by itself. Xenia Edge applies it at
  launch from `saves/0000000000000000/<title>/000B0000`; a refused update is logged as
  `Skipping incompatible title update` (that warning no longer touches a window the PS5 does not
  have).
- **DLC tab**: On / Off / Not installed per package, the reason a file was ignored, and waiting DLC
  installed on Play (the installer now checks packages with Xenia Edge's own container reader).
- **Kinect with a phone, without a PC**: the camera page and its HTTPS server run on the console
  (mbedTLS 3.6.7, fetched by the build script); scan the Kinect QR code in Settings. The 0.5.9
  virtual NUI / skeleton / hands work is merged into Edge's Kinect code. Still experimental (Dragon
  Ball Z for Kinect). The page itself comes from PS5X360's `assets/kinect`.
- **Multi-disc games**: the other discs ("Disc N" folders or images beside the first, or a
  `discs.m3u`) are offered in Xenia Edge's own disc swap dialog when the game asks for one; the
  library shows a disc set as one game.
- **Controller keyboard**: games asking for text get PS5X360's on-screen keyboard typed with the
  pad (was: the default text was answered without asking).
- Game FPS overlay also shows the output (presented) rate and whether frame generation is active.
- State of Decay: a bind the console refuses is moved to an endpoint it allows (same port on any
  address, port + 10000, then a system port); refused binds report Winsock codes.
- Bully and other games that keep many files open: read-only game files share at most 64 native
  descriptors, and the descriptor limit is raised within its hard limit.
- Controller: capability queries other than "gamepad only" are answered; a refused vibration no
  longer makes the pad read as disconnected.
- Prince of Persia: The Forgotten Sands: PS5X360's audio deadlock patch file is included.
- The pipeline cache is also saved at exit / restart (PS5X360 0.5.9).
- Taken from Xenia Edge rather than ported, as Edge already does it its own way: a draw waiting
  for its shader (Edge waits only for one-off, small or memexport targets), guest APCs in
  alertable waits and thread suspension races (Edge's guest scheduler).
- Not taken: PS5X360's OpenGL renderer (research, not a supported renderer there either).

## 0.2-alpha.42+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- NFS The Run: visibility queries back to Strict. The no-waiting kind came too late for it: the
  game reads them without waiting, and the hallway light itself stayed in the elevator again.
- Image readback "Fast" now means the new copy without waiting (alpha.41), "Full" waits for the
  current image as before. Both, and Frame pacing, can be switched in the guide while playing,
  to compare them in the same scene: alpha.41's car sequence ran at 20-23 FPS against about 29
  in alpha.39, and the slower part has to be found.
- The performance line splits the GPU waits into frames in flight, export output the game waits
  on, and other.

## 0.2-alpha.41+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

Aimed at a locked 30 FPS with even frame times at 2x (1440p) in NFS The Run.

- **Even frame pacing** (new per-game option "Frame pacing", on for NFS The Run): each frame the
  game makes is shown for exactly two 60 Hz refreshes. Before, the screen took the newest frame
  at each refresh on its own clock, so frames landing near a refresh got 1 refresh and their
  neighbour 3 (16.7 / 50 ms) even at a steady 30 FPS. The emulated console's vblank now runs at
  the display's 59.94 Hz in this mode, and its thread above normal priority.
- **Visibility queries without waiting** (new option "Strict, no waiting", default for NFS The
  Run): the real answer per query, delivered as soon as the GPU has it, without the emulator
  stopping for it. Strict cost up to 90 ms a second of waiting in some scenes.
- **Render output the game reads, without waiting** (NFS The Run): small images it reads back
  about 100 times a second are copied into its memory by the GPU right after they are drawn,
  so its threads no longer stop for the GPU. Reads may see the previous frame's output. Fewer
  early hand-overs to the GPU come with it.
- **Pacing statistics** every 30 s in the log: intervals of the emulated vblank, the game's
  frames, the frames the GPU thread processed and the host presents, and how many refreshes
  each frame was on screen.

## 0.2-alpha.40+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- NFS The Run defaults confirmed on the PS5 (alpha.39):
  - At 2x its render targets up to 320 pixels wide stay native (`draw_resolution_scale_threshold
    = 320`, built in now; a game-configs file can still change it). Its brightness measurement
    came out wrong upscaled and scenes were far darker than at 1x.
  - Visibility queries default to Accurate: lens flares of lights out of view (the hallway light
    in the elevator) no longer stay on screen. The sun's flare still does.
  - The car sequence ran at about 29-30 FPS at 2x (1440p), against 22-23 before.

## 0.2-alpha.39+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Undid alpha.37's fewer GPU submissions by default (`resolve_sync_only_watched = false`). In NFS
  The Run at 2x it halved the submissions (550 to 255 a second), but handing them over took as
  long in total, and the game's reads of render output waited about four times longer (100-270 ms
  a second instead of 30-50), so the frame rate dropped to 21-25 more often.

## 0.2-alpha.38+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fixed a hang in NFS The Run on My Cars caused by alpha.37's fewer submissions: the GPU waited
  for the CPU while the CPU waited to read render output the GPU had not been handed yet. When
  the game makes the GPU wait on memory, all pending render output is now handed over, as
  before, and a CPU read that finds output not yet handed over makes it go at the next chance.

## 0.2-alpha.37+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- The shader cache was thrown away on every launch since alpha.35: the header check ported from
  PS5X360 0.5.9 never matched the PS5 driver's own saved cache ("rejected damaged or incompatible
  header"). It now only logs the difference; the driver itself decides whether the data is usable.
- Fewer GPU submissions. At 2x the emulator handed work to the GPU about 550 times a second, most
  of them early so the CPU could read render output right after a fence, and each hand-over took
  up to half a millisecond. Render output the game has not read for a while no longer causes one
  (`resolve_sync_only_watched`, under [GPU]).

## 0.2-alpha.36+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Every launch logs "no stored pipelines": the list of the game's pipelines, kept so they can be
  rebuilt before they are needed, is never found again, so each session builds them as the game
  meets them. The log now says how many shaders and pipelines storage loaded, and each shader
  cache save says how large the storage files have grown, to find where they are lost.

## 0.2-alpha.35+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- The NFS The Run fog and smoke flicker is fixed (alpha.34, confirmed on the PS5).
- The performance line every 30 s now also shows how busy the PS5's GPU is, how many times a
  second work is handed to it and why (frame end, guest command buffer end, render output the
  game is about to read, occlusion queries, waits), and how long the emulator waits for it
  (occlusion query results, render output the CPU reads, other).
- "Measure performance" no longer traces GPU frames: reading render output back stalled the GPU
  and skewed the figures. `frame_trace_on_measure = true` under [GPU] brings the trace back.
- From PS5X360 0.5.9: rewriting a shader constant with the same value no longer forces the
  constants to be uploaded again (`vulkan_skip_redundant_constants`), and used constants are
  copied in runs (`vulkan_coalesce_float_constant_copies`). The shader cache file is checked
  (size, read errors, driver header) before it is used, and the save when a game closes writes
  a temporary file first and syncs it, so an interrupted save keeps the previous cache.

## 0.2-alpha.34+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Fix attempt for the NFS The Run fog and smoke flicker. The fog buffers are rendered on the GPU
  and then read as vertex data by the particle draws. Some of those draws also read GPU-exported
  data, so they are routed to the copy of memory the CPU sees, where render output only arrives
  once the CPU reads it. They drew one of the three buffers from its old contents, hence one dark
  frame in three. Running the performance measurement hid it because the trace copies every fog
  buffer to that memory. Render output is now copied there (on the GPU, only the pages such a draw
  fetches) before the draw runs. `host_buffer_resolve_sync = false` under [GPU] turns it off.

## 0.2-alpha.33+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Found the real inputs of the NFS The Run fog buffers: each is drawn from the previous buffer
  and two 256x32 float textures. For two buffers those sit right after the buffer itself; for the
  dark one (0A680000) one of them is far away at 0AEE6000, which the trace never covered. The fog
  buffers are also vertex data for the particle draws, which is why one bad buffer shows up as a
  dark frame of fog and smoke. The trace now follows every texture the fog-buffer draws read, logs
  how the emulator tracks its pages (valid, GPU output, kept through a CPU write), and in its
  second half reloads them all from guest RAM.

## 0.2-alpha.32+0.5.7-fix.1 (PS5X360E, on Xenia Edge)

- Narrowed the NFS The Run fog flicker. Each fog buffer is computed from the previous one, itself,
  and the two 128 KiB objects after it. For the two good buffers the trace shows all four being
  refreshed; for the dark one (0A680000) the object at +0x40000 never is. The other two get it
  refreshed only by accident, because a CPU write right after it shares a 16 KiB PS5 page with it.
  The trace now logs every draw that reads or writes around the buffers (textures, vertex buffers,
  memory exports, and a hash of guest RAM there), and in its second half it reloads those inputs
  from guest RAM. If the dark buffer comes out right in that half, the cause is a stale copy.

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
