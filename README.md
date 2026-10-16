# Downhill Domination Recomp

Static recompilation of *Downhill Domination* (PS2, USA, SCUS-971.77) to native
Linux x86, built on [PS2Recomp](https://github.com/ran-j/PS2Recomp).

**Status:** work in progress, not playable. The recompiled game boots, loads
its data from the disc and reaches the "PRESS X TO START" title screen at full
PS2 speed outside heavy scenes. The 3D scene on the title is mostly missing:
the GS rasteriser is verified correct against PCSX2, so the remaining picture
problems are upstream of it (see [Findings](#findings) and
[Open problems](#open-problems)).

## Legal

This repository contains no game assets, ELF, BIOS or anything derived from
them (the Ghidra project, function lists, binds, GS dumps and generated code
stay local and are git-ignored). You must supply your own legally obtained dump
of the game.

## Setup

    ./setup.sh

Put your ISO at `Downhill Domination (USA)/Downhill Domination (USA).iso`.
The script installs dependencies (Arch/pacman), extracts the disc to `game/`,
clones PS2Recomp at a pinned commit, applies `patches/ps2recomp-runtime-env.patch`
and builds it. Then follow [Workflow](#workflow) to produce `config/`.

Extra tools used by the workflow: Ghidra with the
[Emotion Engine extension](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)
(the release that matches the Ghidra version), `perf` for profiling, `zstd`
for compressed GS dumps, and PCSX2 as the ground truth.

## Workflow

The ELF is retail and has no symbols, so the game's SDK functions (libsif,
libcdvd, fio, memory card, pad, mpeg) run as ordinary recompiled code waiting
for a real IOP. Each one is bound to a runtime HLE stub by address.

1. Analyse `game/SCUS_971.77` in Ghidra headless, then run
   `tools/ghidra/CreateGapFunctions.java` and
   `PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java` in the same pass
   to write `config/config.toml` + `config/functions.csv`.
2. `ps2_recomp config/config.toml`, copy the output into
   `PS2Recomp/ps2xRuntime/src/runner` and `include` with `rsync -rc`, and build.
3. Generate `config/binds.txt` from the SDK signature scanner:
   `PS2Recomp/out/build/ps2xAnalyzer/ps2_analyzer game/SCUS_971.77 config/analyzer/analyzer.toml`
   lists the SDK functions it recognises by byte pattern; bind the ones with a
   runtime handler (except libc and libgraph, see [Findings](#findings)).
4. Iterate with `config/binds.txt` (`handler@0xADDR` lines: a runtime stub
   name, `ret0` or `ret1`) and `./run.sh`. No regeneration or rebuild is needed.
5. Check picture problems with GS dumps instead of running the whole game
   (see [GS dumps](#gs-dumps)).

Scripts and tools:

- `run.sh [secs]` runs the game (`BUILD=fast` selects the build without
  function tracing); output is line-buffered into `run.log`.
- `diag.sh` summarises where the last run stopped.
- `bind.sh` is the slow path (edits `config.toml`, regenerates, rebuilds).
- `rpc_table.py` lists game functions that use IOP RPC (SID and function number).
- `tools/ghidra/DecompileFunctions.java` exports C for a list of addresses:
  `analyzeHeadless ghidra DownhillDom -process SCUS_971.77 -noanalysis -readOnly -scriptPath tools/ghidra -postScript DecompileFunctions.java 0xADDR,0xADDR out.c`
- `tools/ghidra/CreateGapFunctions.java` creates functions in code ranges the
  analysis left undefined (functions reached only through pointers).
- `tools/gsplay/` (`build.sh`, then `gsplay dump.gs[.zst] outdir`) replays a
  PCSX2-format GS dump through the runtime GS and writes each presented frame
  next to the dump's embedded screenshot, with a mean per-channel error.
- `tools/gsdump_stats.py` prints per-frame packets, vertices, primitives and
  registers of a dump; `tools/gsdump_strips.py` lists the first strips of a
  frame with vertex positions, colours and ADC flags.

### GS dumps

PCSX2 saves single-frame GS dumps (Settings → Hotkeys → *Save Single Frame GS
Dump*) with a screenshot embedded. The runtime records the same format with
`PS2X_GSDUMP`. Replaying either through `gsplay` and comparing streams with
`gsdump_stats.py` separates the stages: a PCSX2 dump that renders right in
`gsplay` proves the GS; a runtime dump that differs from PCSX2's in vertices or
packets points at EE, VIF or VU1.

### Environment variables

All come from `patches/ps2recomp-runtime-env.patch`:

| Variable | Effect |
| --- | --- |
| `PS2X_CD_IMAGE` | ISO used for raw sector reads; `sceCdSearchFile` then returns the file's real ISO sector |
| `PS2X_BINDS` | file of `handler@0xADDR` lines applied at ELF load |
| `PS2X_DUMP_RAM="secs:path"` | writes the 32 MB EE RAM once after that many seconds |
| `PS2X_TRACE_CALLS=0xA,0xB` | logs calls to those guest addresses with arguments and the VSync tick |
| `PS2X_TRACE_LIMIT=N` | how many `PS2X_TRACE_CALLS` lines to print (default 400) |
| `PS2X_TRACE_SYSCALLS=N` | logs the first N thread, semaphore and interrupt syscalls (3000 if N is 1) |
| `PS2X_VSYNC_LOG=1` | every 60 VSyncs prints emulated VSyncs per real second (60 = PS2 speed) |
| `PS2X_PROF=1` | VU1/GS time shares every 10 s, and VU1 runs that end normally vs by cycle budget |
| `PS2X_GSDUMP="vsync:frames:path"` | records everything that reaches the GS as a PCSX2-format dump |
| `PS2X_IOP_TRACE=N` | logs the first N import calls made by IOP modules (library:ordinal, args, result) |
| `PS2X_AUTOX=<secs>` | pulses X on the pad from that second on, for reproducible runs |

The patch also makes the scheduler match the real kernel (main thread at
priority 1, priority 0 accepted by `CreateThread`), makes the simulated
`sceCdRead` take one frame, batches IOP cycles, and adds the COP0
branch-condition case to the recompiler (`control_flow_emitter.cpp`; rebuild
`ps2_recomp` and regenerate for it to apply).

## Findings

What each fix or measurement established, in the order it was found.

**Analysis and binding**

- `ps2_analyzer`'s embedded SCE signature database recognises 376 SDK functions
  in the ELF by byte pattern; 126 have a runtime handler. Binding them replaces
  identifying SDK functions one by one. A few variants are missing from the
  database (the CD file search was found by its string references instead).
- Ghidra leaves functions reached only through pointers undefined, and calls
  into them end in `missing-target`. `CreateGapFunctions.java` fixes them in one
  pass (about 340 functions over repeated passes) instead of one per run.

**Speed**

- Do not bind libc (`memcpy`, `memset`, ...) or libgraph (`sceGsSyncV`, ...) to
  HLE stubs. A function bound by address returns to the EE scheduler on every
  call; tight loops of `memcpy` ran the game at about 30% speed, and unbinding
  them gave a steady 60 VSyncs per second. The libgraph bind hung the boot.
- Busy-wait loops (pad polling during loading) are cheap on a PS2 but each
  iteration cost the full scheduler path. `processPendingEvents` now returns
  early when nothing is due, and `GIF_STAT` is cached instead of looked up in
  the I/O register map on every dispatch.
- With `perf`, `FUN_<addr>` symbols in the generated code show which game
  functions are hot; game code at 0% means the EE is waiting, not computing.

**Picture**

- The GS rasteriser is correct. PCSX2 dumps of the title and rider select
  screens replayed through `gsplay` match PCSX2's frame with a mean error of
  about 5/255 (bilinear filtering and deinterlacing on PCSX2's side). The only
  bug found was in presentation: with `SMODE2` INT=1 FFMD=1 each field reads a
  half-height buffer, which the runtime showed squashed and duplicated.
- On the title, the runtime sends the GS about 2% of PCSX2's vertices (1.7k vs
  89k per frame) and the vertices it does send sit almost on one point with
  black lighting. The VU1 interpreter is not cutting programs short (every run
  ends by E-bit, none by cycle budget). Every new guest thread used to start
  with VU0 `vf0` = (0,0,0,0) instead of the constant (0,0,0,1); that is fixed,
  but it did not change the title (it is drawn by the main thread).

**Boot path**

- Intro movies need the IPU fed by DMA, which the runtime does not emulate;
  they are skipped with `ret0`/`ret1` binds on the movie start and on the
  movie sequence loop.
- The EE CD HLE used to hand out invented sectors from 0x100000 while the
  emulated IOP reads the ISO by real sector, so IOP modules given an EE search
  result (989snd loading sound banks) read the wrong place. With
  `PS2X_CD_IMAGE` the EE now returns real ISO sectors.
- The emulated IOP ran RPC server functions synchronously, outside any IOP
  thread. Drivers that sleep inside a request waiting for another thread
  (989snd waits for its CD thread while loading a bank) spun forever because
  `SleepThread` had no current thread to put to sleep. That left the loading
  screen up forever; returning failure from the bank load on the EE got to the
  title, but the sound sequencer then called through garbage data (the late
  crash).

## Open problems

- **Threaded IOP RPC servers (in progress, not yet in the patch):** run each
  server function inside the IOP thread that registered it and complete the EE
  call later (NOWAIT returns at once, blocking calls wait like libsif's
  `WaitSema`). With it, sound banks load through the real 989snd and the late
  crash is gone. Servers that wait on hardware the IOP does not emulate (SIO2
  for multitap) need a timeout fallback, and each HLE RPC is charged EE cycles
  so loops that resend commands let the IOP progress. Progress after the title
  still varies between runs.
- **Music streaming:** 989snd's VAG streaming fails ("Not in any buffer ...
  Stopping stream") because LIBSD/SPU2 is HLE only; the game then waits for its
  stream stop commands to drain and stops drawing.
- **3D geometry on the title:** about 2% of PCSX2's vertices, collapsed and
  unlit. Next step: compare VU1 input (VIF1 data, VU memory) and the VU0 macro
  and micro results that build the matrices against PCSX2.
- **Speed in heavy scenes:** about 31 VSyncs per second when VU1 and the GS
  work hard (see [docs/performance.md](docs/performance.md)).

## Roadmap

1. **Finish threaded IOP RPC servers** and make the boot deterministic past
   the title.
2. **Find why VU1 output is collapsed:** diff VIF1/VU1 input and VU0 results
   against PCSX2 with the same dump-and-compare method used for the GS.
3. **Get through the menus into a race** and fix whatever breaks on the way.
4. **Replace the temporary shortcuts:** intro movies (IPU DMA path) and music
   streaming (SPU2).
5. **Make it playable:** confirm input, then run a race end to end.

## License

GPL-3.0, matching PS2Recomp (the generated code links against its runtime).
