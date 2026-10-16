This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Static recompilation of *Downhill Domination* (PS2, USA, `SCUS_971.77`) to native Linux x86 on top of [PS2Recomp](https://github.com/ran-j/PS2Recomp). Progress, findings and roadmap live in `README.md`; do not duplicate them here. The user writes in Portuguese; scripts are commented in Portuguese, the README in English.

## How to work on this project

The first attempt took ~50 sessions of run-crash-guess and ended on a solid-colour screen. The method that replaced it:

1. **Measure before changing.** State the hypothesis, add a counter or trace that can prove it wrong, then change code. Two plausible causes (VU1 cycle budget, `vf0`) were ruled out this way in minutes.
2. **Isolate stages with differential tests against PCSX2.** For the picture, use GS dumps: a PCSX2 dump replayed in `tools/gsplay` tests the GS alone; a runtime dump (`PS2X_GSDUMP`) compared with `tools/gsdump_stats.py` / `gsdump_strips.py` shows whether EE/VIF/VU1 send the GS the same thing PCSX2 does. Apply the same idea to the next stage (VU1 input, VU0 results) instead of reading the whole pipeline.
3. **Fix classes of problems with tools, not one address per run.** SDK functions come from `ps2_analyzer`'s signature database; undefined pointer-only functions come from `tools/ghidra/CreateGapFunctions.java`.
4. **Profile with `perf`**, not gdb samples. Generated functions keep their `FUN_<addr>` names, so `perf report --sort sym` shows hot game code; 0% game code means the EE is waiting. For call chains use `--call-graph dwarf` on the `GameThread` thread.
5. **Fix the cause in the runtime when it is the runtime's bug** (patch it and export the patch) rather than stacking `ret0` binds. Use a bind as a temporary shortcut only to reach the next stage, and say so in its comment.

## What is and is not in this repo

Only our own work is versioned: `setup.sh`, `run.sh`, `bind.sh`, `diag.sh`, `rpc_table.py`, `tools/` (Ghidra scripts, `gsplay`, dump scripts), `patches/`, docs. Everything else is local and git-ignored by root-anchored rules: `PS2Recomp/` (third-party clone), `game/` (extracted ISO), `config/` (Ghidra export, generated code, `binds.txt`, analyzer output, GS dumps, decompiled C), `ghidra/` (Ghidra project), BIOS, ISO, logs, perf data.

- **Never commit anything derived from the game**: decompiled C, `functions.csv`, generated `.cpp`, binds, dumps, address lists. The README deliberately lists no game addresses.
- **Edits inside `PS2Recomp/` (ps2xRuntime, ps2xIOP, ps2xRecomp) are lost unless exported** to `patches/ps2recomp-runtime-env.patch`, pinned to PS2Recomp commit `2c5fbb9`. Regenerate it with `git -C PS2Recomp -c diff.mnemonicPrefix=true diff -- <changed source files> > patches/ps2recomp-runtime-env.patch` (keep the `i/`/`w/` prefixes so commits show only real changes; `git add -N` new files first). Check it applies to a clean checkout of `2c5fbb9`. Never include `ps2xRuntime/src/runner/*`, generated headers in `ps2xRuntime/include/` (`FUN_*`, `entry_*`, `caseD_*`, `ps2_recompiled_*`) or `ExportPS2Functions.properties`.
- **One commit per logical change, also inside the patch.** The patch is one file, so build it in steps: `git -C PS2Recomp worktree add --detach PS2Recomp/out/patchwork 2c5fbb9`, apply the committed patch there, copy one group of changed files at a time, regenerate the patch from the worktree, commit, repeat; at the end check every patched file matches `PS2Recomp/` and remove the worktree.
- Keep `.gitignore` rules anchored with a leading `/`. An unanchored `ghidra/` once silently ignored `tools/ghidra/`.

## Builds

CMake + Ninja under `PS2Recomp/out/`, all with `-march=native` (the runtime needs SSE4). The machine has ~7 GB RAM: use `-j4` and never run two big builds at once.

| Dir | Purpose | Notes |
| --- | --- | --- |
| `out/build` | Release; builds `ps2_recomp` and `ps2_analyzer` | build those targets only (seconds); the full LTO build is ~14 min and not needed |
| `out/fast` | `RelWithDebInfo` with `CMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -DNDEBUG"` (no LTO, no `-g`), `PS2X_ENABLE_AGRESSIVE_LOGS=OFF`, `PS2X_BUILD_RECOMP=OFF` | the build to run and measure; full rebuild ~11 min |
| `out/dev` | `-O1`, aggressive logging on | optional; writes `ps2_log.txt` at ~100 MB/min |

    cmake --build PS2Recomp/out/fast --target ps2EntryRunner -j4
    BUILD=fast ./run.sh [secs]     # line-buffered into run.log
    ./diag.sh                      # summarises the last run
    ./bind.sh name@0xADDR ...      # slow path: edit config.toml stubs, regenerate, rebuild

**What triggers an 11-minute rebuild:** any header the generated code includes (`ps2_runtime.h`, `runtime/ee_scheduler.h`, `runtime/ps2_memory.h`, `runtime/gs/gs_frontend.h`, `ps2x/iop/iop_types.h`, `ps2x/iop/iop_host.h`, ...) and any regeneration of `config/output`. A change to a `.cpp` or an internal header (`Kernel/Stubs/Helpers/*.h`, `Kernel/Syscalls/*.h`, `ps2xIOP/src/**`) relinks in seconds. Prefer file-local state in `.cpp` files for instrumentation and caches; batch header changes.

Regenerating code: `PS2Recomp/out/build/ps2xRecomp/ps2_recomp config/config.toml` writes `config/output/`; copy into `ps2xRuntime/src/runner` (`.cpp`) and `include` (`.h`) with **`rsync -rc`** (26k files exceed the argument limit; checksum copy avoids rebuilding everything). The runner globs with `CONFIGURE_DEPENDS`, so new functions are picked up.

## Ghidra (headless)

Project `ghidra/DownhillDom`, language `r5900:LE:32:default`, Emotion Engine extension in `~/.config/ghidra/ghidra_12.1.2_DEV/Extensions`. Run from the repo root:

    /opt/ghidra/support/analyzeHeadless ghidra DownhillDom -process SCUS_971.77 -noanalysis \
      -scriptPath "tools/ghidra;PS2Recomp/ps2xRecomp/tools/ghidra" \
      -postScript CreateGapFunctions.java -postScript ExportPS2Functions.java
    /opt/ghidra/support/analyzeHeadless ghidra DownhillDom -process SCUS_971.77 -noanalysis -readOnly \
      -scriptPath tools/ghidra -postScript DecompileFunctions.java 0xADDR,0xADDR out.c

`ExportPS2Functions.java` reads its two output paths from `ExportPS2Functions.properties` next to the script, keys `Choose output TOML config file Save` and `Choose output CSV file Save` (unescaped spaces). The Ghidra GUI must be closed while a headless run writes the project. Do not read raw MIPS when the decompiler can give C; walk up callers (`// callers:` in the output) until the loop or condition that matters.

## How the pieces fit

The ELF is retail with no symbols. The **SDK libraries (libsif, libcdvd, fio, libmc, libpad, libmpeg, 989snd's EE side) are ordinary game code** that talks RPC to the IOP. `ps2_analyzer` (`PS2Recomp/out/build/ps2xAnalyzer/ps2_analyzer game/SCUS_971.77 config/analyzer/analyzer.toml`) recognises most of them by byte pattern; `config/binds.txt` is generated from its `stubs` list, grouped by library with comments.

- **Do not bind libc or libgraph.** A function bound by address returns to the EE scheduler on every call: HLE `memcpy`/`memset` made tight loops ~3x slower than the recompiled originals, and binding `sceGsSyncV` hung the boot. Leave them recompiled.
- Two ways to bind: `config/config.toml` `stubs` (baked in, needs regenerate + rebuild) or `config/binds.txt` (`handler@0xADDR`, applied at ELF load through `PS2X_BINDS`, no rebuild). Prefer `binds.txt`. State your confidence in its comment.
- Generated naming: `FUN_<addr>_0x<addr>` is a Ghidra function; `entry_*`/`caseD_*` are fragments of the function before them. `config/functions.csv` is `name,start,end,size`.
- A stub that blocks the thread (`waitVSync`, `waitExternal`) must set `ctx->pc = $ra` first.
- Runtime layout (`PS2Recomp/ps2xRuntime/src/lib`): `Kernel/EeScheduler.cpp` (guest threads, VBlank, invocations; `processPendingEvents` runs twice per dispatch, keep it cheap), `Kernel/Syscalls` (incl. `RPC.cpp`, the EE side of SIF RPC), `Kernel/Stubs` (HLE of CD, MC, pad, MPEG, FileIO; CD sector mapping in `Helpers/Support.h`), `gs/` (software GS, verified against PCSX2), `vu/` (VU1/VU0 interpreter), `ps2_runtime.cpp`.
- `PS2Recomp/ps2xIOP` runs the game's real `.IRX` files on an R3000 interpreter with HLE imports (`src/emulator/imports`, kernel in `src/emulator/core/iop_kernel.cpp`, RPC bridge in `src/emulator/services/iop_rpc.cpp`). The IOP advances only through EE cycles (`advanceIopEeCycles`), so EE loops that burn host time without charging cycles starve it. RPC server functions must run inside their IOP thread or drivers that sleep in a request never finish. SIO2 and SPU2 streaming are not emulated.
- The IOP `cdrom0:` root is `game/`; raw sector reads and real ISO sectors need `PS2X_CD_IMAGE` (`run.sh` sets it and `PS2X_BINDS`).
- Threads/priority follow the real BIOS: main thread priority 1, `CreateThread` accepts priority 0. Do not "fix" priority 0 back to invalid.

## Debug toolbox

Environment variables (table in the README): `PS2X_VSYNC_LOG` for speed, `PS2X_PROF` for VU1/GS shares and VU1 exit reasons, `PS2X_TRACE_CALLS` + `PS2X_TRACE_LIMIT` (lines carry `vs=` VSync tick), `PS2X_TRACE_SYSCALLS`, `PS2X_IOP_TRACE` for IOP module imports, `PS2X_GSDUMP` for GS dumps, `PS2X_DUMP_RAM`, `PS2X_AUTOX`.

- Tracing a hot address distorts timing (printing tens of thousands of lines per second); measure speed with `PS2X_VSYNC_LOG`, not with a trace running. Remove any temporary `fprintf` from hot paths before measuring and before committing.
- Run-to-run timing varies (host pacing); confirm a regression with a second run before chasing it.
- PCSX2 (Flatpak, `~/.var/app/net.pcsx2.PCSX2/config/PCSX2/snaps/` for dumps and screenshots; BIOS: USA 1.90) is the ground truth for pictures, the thread table and memory-write breakpoints. Keep dumps under `config/dumps/`.

## Working with the user

- Reply in Portuguese. Explain findings with the evidence (numbers, traces) and say plainly when something is not verified.
- **Do not install anything.** When a tool would help (e.g. `perf`), stop and ask the user to install it with the exact `sudo pacman -S ...` command instead of building a workaround.
- Screen captures: ask the user for screenshots instead of capturing their desktop. The game window is titled `PS2-Recomp | SCUS_971.77`; give the user the exact command and controls when they should look at it.
- Long builds/runs go in the background; report progress when asked.
- Commits: check `git config user.name`/`user.email` first and ask if missing (never invent an identity); work on a branch, not `main`; group commits by logical change (runtime patch steps, scripts, tools, docs separately); push over SSH (`origin` is `git@github.com:pedrohenrique505/Recomp-Domination.git`) when the user asks.
