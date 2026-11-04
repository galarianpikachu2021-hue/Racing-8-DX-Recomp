# From NSP to a playable game: the self-contained builder (design)

Date: 2026-11-04. Status: draft for approval. Decided by the user unless marked "my call".

## Intent

A person with their own Mario Kart 8 Deluxe files installs Racing 8 DX Recomp (a folder on Windows, an AppImage on Linux), opens the
launcher, picks their files, presses **Build**, waits a few minutes, presses **Play**. Nothing else to install, nothing to download
during the build. Today the recipe (docs/PIPELINE.md) is a ten-step manual process that needs a source checkout, the suyu GUI, a Python
tool chain and hours of compiling; only the author can run it.

## Goals

1. The launcher opens to a **Setup** page with one simple file picker per input: base game NSP, update 3.0.4 NSP, optional DLC NSPs,
   `prod.keys` (and `title.keys` if present), firmware (folder or zip). Each picker validates its file and says plainly what is wrong
   (wrong update version, missing key, firmware too old).
2. **Build game** runs the whole pipeline with one progress bar, can be cancelled and resumed, and ends in **Play**.
3. Every other launcher tab (Enhancements, Mods, controllers, resolution, upscalers, frame generation, HDR) works as on Linux today.
4. Fast: the compile step is the tiered clang build already measured (about 7 minutes wall on 8 threads and 16 GB, 26.6 CPU-minutes).
   The engine itself is never compiled on the user's machine.
5. Works on **Windows and Linux** together. Linux is the reference (everything exists and is verified); Windows is the port.
6. Self-contained: everything needed except the user's files ships in the Windows folder / Linux AppImage.
7. Strict **no JIT**, on both platforms, for ever. (User rule.)

## Non-goals

- No "package as exe" / installer for the finished game on Windows (the Linux "package as AppImage" button stays as is).
- No game files, keys, firmware, generated C or built game code in any release or repository. Releases contain the launcher, the engine
  built from the GPL fork (no game code), the exporter, a compiler and runtime libraries.
- No support for other game versions: only update 3.0.4 (the v1.0.0 base and 3.0.3 are 32-bit).
- Android is a separate track (plans 1, 1b, 2, 3); this design is desktop only.
- No texture upscaling, no CTGP (paused), no Windows driver tuning beyond what is needed to run.

## Anti-goals (a failure even if it technically works)

- The user is asked to install a compiler, Python, Qt, CMake or Visual Studio.
- The build needs the internet after install, or silently fetches code.
- A missing translated block falls back to a JIT or interpreter.
- A build that quietly produces a game that crashes later: the pipeline must verify itself (coverage says `fully static`, a smoke run
  reaches the title screen) before it enables Play.

## What is already there (Linux)

- Exporter: the suyu GUI's AOT export (`suyu/game_export.cpp`, driven over an MCP port by the author's scripts, with an "export profile"
  that has the update installed in NAND). It writes the C tree: `rtld/main/subsdk0/sdk` units, `recomp_*` headers, a CMake project.
- Unit splitting for fast compiles: `tools/recomp/split_units.py`, `split_export.py --hot=data/mk8d-3.0.4/hot_blocks.txt --strip-hooks`
  (hot blocks `-O2`, cold `-O0`, 5000-block units, clang).
- Link into the engine (`suyu-cmd-static`, strict no-JIT): today a CMake build of the whole fork with the generated C inside it
  (2.5 h with gcc, much less with clang tiered, still a full fork build when the engine changes).
- Launcher (Qt 6) with game folder, mods, DLC, enhancements; updater; Linux AppImage packer for the finished game.
- Android's CMake integration already shows the generated C can be built as a separate static module that registers itself with the engine
  (`kartrecomp_static_recomp.cmake`, `KartStaticRecomp::Register`).

## The shape of the solution (my call, to be confirmed by the plan)

Split the game binary into a **prebuilt engine** and a **per-user game module**:

```
release folder / AppImage
  kartrecomp(.exe)         launcher (Qt)
  engine/  mk8d-engine     the fork's runtime (HLE services, native renderer, plugins), built once by us, no game code
  tools/   exporter        headless NSP -> C exporter (new, see below)
           clang + lld     bundled toolchain with the target's C runtime headers and libs
           split/unit scripts (rewritten as part of the launcher or a small bundled tool: no Python on the user's PC)
  data/    hot_blocks.txt
user data (per user, XDG on Linux, %APPDATA% on Windows)
  build/   generated C, objects, the game module (game.so / game.dll)   <- derived from the game, never distributed
  nand/, saves, mods, keys, logs
```

- The engine loads the game module at start (a shared library built from the generated C) and registers its blocks the way the Android
  build does with its static modules. No runtime code generation anywhere: the module is ordinary ahead-of-time compiled code. This is
  the only structural change to the game binary and must keep the strict static guarantees (every guest instruction in the module,
  `fully static` coverage check).
- Build = export, split, compile units in parallel (job count from free RAM, 2 GB per job), link the module, verify. Incremental and
  resumable: finished units are kept.

## The pieces

1. **Headless exporter.** The export exists only inside the Qt GUI today. It must become a command-line tool that takes the base NSP, the
   update NSP, keys and firmware and writes the C tree, without a GUI, an MCP port or a hand-made NAND profile. Windows needs it too.
2. **Module split of the engine** (engine + loadable game module), including the registry/handshake that today is generated at export
   time, ABI versioning between engine and module, and the release engine built once per platform.
3. **Bundled toolchain.** clang + lld (LLVM, Apache-2.0 with LLVM exceptions) on both platforms. Linux needs a sysroot (C headers,
   libc) that does not depend on the user's distro having dev packages. Windows cannot redistribute MSVC or the Windows SDK, so the
   Windows engine and module target the MinGW-w64 environment (llvm-mingw) *(my call)*; the compiler flags (`-march` x86-64-v3, hot/cold
   tiers) are the same as Linux.
4. **Windows port of the engine**: fork, native Vulkan renderer (SDL3 window, `NativeSurface`), input, timing, memory model for the
   guest address space (fastmem/mmap equivalents), thread pinning, plugin loading (`.dll`), logging paths. Mostly platform glue; the
   recompiled code itself is plain C.
5. **Launcher Setup page and build runner**: file pickers, validation (NSP/key/firmware checks, update version via the cnmt reader that
   exists as `tools/nsp-cnmt-version.py`), progress and ETA from unit counts, cancel/resume, clear failure messages, a log the user can
   attach to a bug report, then verification and Play.
6. **Verification step**: coverage check (`fully static`) and a short headless smoke run to the title screen before Play is enabled; on
   failure show what failed, never silently continue.
7. **Packaging**: Windows folder (zip) and Linux AppImage both contain the launcher, engine, exporter and toolchain. Release workflows
   build and attach them (the Windows launcher workflow already exists; it grows to the full bundle).

## Constraints

- Legal: nothing derived from the game is distributed (LEGAL.md). The exporter and compiler run only on the user's machine on their own
  files. The upstream mk8-recomp has no license: reference, never copy its scripts (the headless exporter is our own code in the fork).
- Memory: peak per compile job stays under ~1 GB (clang tiered); job count follows free RAM; the machine need not be idle.
- CPU: the generated code targets x86-64-v3 (AVX2, BMI2, FMA, MOVBE) on desktop, as today; the launcher refuses with a clear message on
  older CPUs.
- Size: the bundle (engine, compiler, runtime) stays reasonable to download; target well under 1 GB, measured in the plan.
- The user's strict rule: **no JIT** anywhere. Other decisions are negotiable but ask first.

## What is left to the builder

- Exact on-disk layout and names, build cache format, hashing to detect an unchanged input.
- How the engine finds and loads the game module (dlopen/LoadLibrary and a stable registration ABI).
- Whether the splitting/unit scripts stay Python (bundled) or move into C++.
- Order of work: Linux end to end first (headless exporter, engine/module split, bundled toolchain, Setup page), then the Windows port.
- The test plan: a CI-less local acceptance run on Linux (clean machine path: AppImage + the author's files -> Play), then the author tests
  the Windows folder on their Windows PC.

## Calls I made after the builder check (mine, not the user's; change them here if wrong)

- **Hooks:** the shipped module is the **unhooked** build (hooks are opt-in at export, `--strip-hooks`); Mods stay data-only, CTGP stays paused.
  Per-game hook offsets are never shipped.
- **Build limits:** tiered 5000-block units peak about 0.4 GB (clang) per job on the real export; minimum 8 GB RAM and 4 threads for the
  advertised few-minutes build. Below that the build still runs with fewer jobs and says it will take longer; it only refuses when free
  RAM is under about 3 GB.
- **Toolchain target (Linux):** glibc, dynamic, C-only module (no C++ ABI crossing), built on an older base so the AppImage runs on
  common distros; a bundled sysroot so the user's distro needs no dev packages. Engine and module agree on one libc and TLS model.
- **Smoke check:** Null-renderer boot (no GPU or window needed) until the coverage log says `fully static` and the title-screen frame
  count is reached; never needs a real display.
- **Version rules:** only the 3.0.4 update (version 20<<16) is accepted; anything else is rejected with the reason (3.0.3 and 1.0.0 are
  32-bit; 4.0.1 is a different, unsupported build until separately asked). Firmware: whatever lets the build boot; the check is the
  Null-renderer boot, plus a clear "firmware missing" message.
- **Existing installs:** the builder is for fresh installs; the author's current `local/game` layout keeps working unchanged. No migration.
- **Export reproducibility:** the plan starts with a spike that checks a fresh export from the current fork reproduces today's tree (hooks
  stripped, chaining, cold attribute); the headless exporter is "extract the GUI's export code", unless the spike finds hand-edited
  state outside the generator, in which case fixing the generator comes first.

## Open decisions for the user

1. Performance budget for the engine/module split (see the question asked in chat).
2. Whether the FSR 3 / DLSS plugins ship in the first Windows build.
