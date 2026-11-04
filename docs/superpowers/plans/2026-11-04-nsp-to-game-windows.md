# NSP-to-game builder, Windows: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Windows zip of Racing 8 DX Recomp where the user picks their own NSPs, keys and firmware in the launcher, presses Build, and plays: the same pipeline as Linux, with the FSR 3 and DLSS plugins built against the current Windows SDKs.

**Architecture:** There is no Windows machine here, so everything is built and tested on GitHub's Windows runners (`windows-latest`), with the user's Windows PC for the final run. The fork (pinned upstream commit plus `patches/suyu/*.patch`) is built there into `mk8d-engine.exe` and `kartrecomp-export.exe`; the launcher and the `kartrecomp-setup`/`-build` tools are built there too; the game modules are compiled on the user's PC into `recompiled_*.dll` by a bundled **llvm-mingw** clang (MSVC and the Windows SDK cannot be redistributed). The engine loads those DLLs through the loader that already exists (`LoadModuleDir`, `LoadLibrary` path).

**Tech Stack:** GitHub Actions (windows-latest, MSVC/clang-cl, vcpkg as the fork already uses), CMake/Ninja, Qt 6 + windeployqt, llvm-mingw, PowerShell, the vendors' current Windows SDKs for the plugins.

**Spec:** `docs/superpowers/specs/2026-11-04-nsp-to-game-builder-design.md` (Linux counterpart plan: `2026-11-04-nsp-to-game-linux.md`, done on branch `nsp-builder`)

## Global Constraints

- Strict no JIT, every platform, forever (`SUYU_NO_JIT=ON`; a missing block is a bug, never a fallback).
- Nothing derived from a game is committed, uploaded to CI, or shipped: CI never sees game files; the zip holds the launcher, tools, engine, toolchain and plugins only.
- Only update 3.0.4 is accepted (unchanged rules in `setup_validate.cpp`).
- Speed no slower than the static build (hard requirement from the spec); measured on the user's PC where a baseline exists.
- The plugins are built against the CURRENT Windows SDKs, not ported from the Linux plugins; if a vendor offers a newer upscaler only on Windows, report it to the user before choosing (the user decides new upscalers; FSR 4 waits for a Vulkan release from AMD).
- Vendor SDK libraries are fetched at build time and never committed; the DLSS plugin is built but its NVIDIA libraries are never redistributed.
- Upstream mk8-recomp has no licence: reference, never copy.
- **Nothing is pushed to GitHub without the user's explicit OK.** CI needs a pushed branch: the first step of Task 1 is to ask for permission to push the branch `windows-port` (public repo; it contains only our patches, workflows and scripts). Commits use the GitHub noreply email.
- A functional Windows build goes to the USB drive labelled "racing 8 testing" (`/run/media/carter/racing 8 testing`), never with game files.

## Review Focus

- A path with spaces and non-ASCII characters (Windows user names, `OneDrive\Documents`) in the data folders and NSP locations.
- A drive that does not support symlinks (no developer mode, FAT/exFAT): NSPs fall back to copy with an honest free-space check, never a silent failure.
- Windows Defender / SmartScreen delaying or quarantining a freshly compiled module DLL: the build must report "cannot load the module" with the OS error, not hang.
- Closing the launcher during a build: every child process (clang, exporter, verify engine) dies with it (Job Object), nothing keeps running.
- Less than the needed RAM / disk, a build interrupted by sleep or a reboot: resumes cleanly, never trusts a half-written object.
- A module built for another engine or ABI: refused with "rebuild the game" (exit 4), same as Linux.
- No GPU / a GPU without the Vulkan extensions the renderer needs: one clear message, not a crash.

---

### Task 1: The fork builds on Windows CI (engine + exporter)

**Files:**
- Create: `.github/workflows/windows-engine.yml` (triggers: push to branch `windows-port` and `workflow_dispatch`)
- Create: `packaging/windows/fetch-fork.sh` (clones the pinned upstream fork commit from `docs/PIPELINE.md`, applies `patches/suyu/*.patch`, fails if one does not apply)
- Create: `tests/test_module_guard.ps1` (Windows twin of `tests/test_module_guard.sh`)
- Modify: fork sources via the patches as needed (compile errors found by CI)

**Interfaces:**
- Produces: workflow artifact `windows-engine` holding `mk8d-engine.exe`, `kartrecomp-export.exe`, their DLLs and `ENGINE_VERSION` (same hash rule as `packaging/build-engine.sh`: sha256 of the patch files + `-abi6`).
- Consumes: `patches/suyu/0001..0003` (they must apply to the pinned commit).

- [ ] **Step 1:** Ask the user for permission to push the branch `windows-port` to the public repo; stop until answered. (Expected: yes/no.)
- [ ] **Step 2:** Write `tests/test_module_guard.ps1`: runs `mk8d-engine.exe` with no module folder and expects exit code 5 with "No game has been built yet", then with a stub module DLL of ABI 99 (built by the runner's clang) and expects exit 4 mentioning "ABI 99". Run the workflow with the engine step missing; expected FAIL (no engine).
- [ ] **Step 3:** Write the workflow: checkout, `fetch-fork.sh`, configure the fork with its vcpkg setup (`-DSUYU_NO_JIT=ON -DKARTRECOMP_ENGINE=ON`, no Qt GUI), build `mk8d-engine` and `kartrecomp-export`, upload the artifact. Fix every compile error in the patches (iterate on CI logs); each fix is re-exported as a patch.
- [ ] **Step 4:** Add the guard test to the workflow and run it; expected PASS (exit 5, exit 4 messages).
- [ ] **Step 5:** Commit on `windows-port`; record the CI run URL in the ledger.

### Task 2: Windows ports of the Linux-only code in the fork

**Files:**
- Modify (via patches): `src/suyu_cmd/cpu_topology.h` (hybrid-CPU P-core pinning: `GetLogicalProcessorInformationEx`), `src/suyu_cmd/sampling_profiler.cpp` (no-op on Windows), `src/suyu_cmd/native_gpu/upscaler.cpp` (plugin loading with `LoadLibrary`/`GetProcAddress`, `.dll` names), `src/suyu_cmd/native_gpu/vk_shadow_backend.cpp` (`sched_setaffinity` → `SetThreadAffinityMask`), `src/suyu_cmd/emu_window/*.cpp` (X11/Wayland-only branches), `src/suyu_cmd/kartrecomp_export.cpp` (`mkdtemp`, already guarded)
- Test: `tests/windows/test_platform.ps1`

**Interfaces:**
- Produces: the engine honours `KARTRECOMP_PIN=<cpu list>` and `KARTRECOMP_PIN_PCORES` on Windows, loads `plugins\kr_upscaler_<name>.dll`, creates its window with SDL3's Windows video driver, and starts with `SDL_VIDEODRIVER=dummy` (the verification boot).

- [ ] **Step 1:** Write `tests/windows/test_platform.ps1`: runs the engine headless with `KARTRECOMP_VERIFY_BOOT=1 KARTRECOMP_VERIFY_DEADLINE=3` and no module (expects exit 5, not a crash), starts it with `KARTRECOMP_PIN=0,1` and checks the log line that reports the pinned CPUs, and loads a stub plugin DLL from `plugins\` expecting the log line "plugin kr_upscaler_stub loaded". Run it on CI; expected FAIL on the missing Windows code paths.
- [ ] **Step 2:** Implement each port with the smallest change (`#ifdef _WIN32` beside the Linux code, same log lines as Linux).
- [ ] **Step 3:** Run the test on CI; expected PASS. Re-export the patches.
- [ ] **Step 4:** Commit.

### Task 3: llvm-mingw toolchain and module DLLs

**Files:**
- Create: `packaging/windows/make-toolchain.sh` (assembles `toolchain/` from a checksum-pinned llvm-mingw release: `bin/clang.exe`, `bin/ld.lld.exe`, the mingw-w64 headers/CRT/libs, compiler-rt builtins; run on the CI runner)
- Modify: `app/src/build_runner.cpp` (`BuildPlan::useToolchain` for Windows: `--target=x86_64-w64-mingw32`, `-shared`, output `recompiled_<name>.dll`, no `-fPIC`, `-fvisibility` replaced by the exports the module sources declare), `app/src/setup_paths.cpp` (`IsBuilt` checks `recompiled_image.dll`)
- Test: `tests/windows/test_toolchain.ps1`

**Interfaces:**
- Produces: `BuildPlan::useToolchain(dir)` picks the platform's compiler and link line; `BuildRunner::moduleFileName(name)` returns `recompiled_<name>.so` / `.dll`.

- [ ] **Step 1:** Write `tests/windows/test_toolchain.ps1` (the Windows twin of `tests/test_toolchain.sh`): with a PATH holding no compiler, the bundled clang compiles and links a module-style DLL that exports `recomp_image_abi`, `LoadLibrary`s and calls it, and the result needs no DLL beyond `KERNEL32`/`msvcrt`/`ucrtbase` (checked with `llvm-objdump -p`). Run on CI; expected FAIL (no toolchain).
- [ ] **Step 2:** Implement `make-toolchain.sh`, `moduleFileName`, and the Windows branch of `useToolchain`; add a unit test `modulesNameFollowThePlatform` to `tests/test_build_runner.cpp`.
- [ ] **Step 3:** Run both on CI; expected PASS. Record the toolchain size (target under 400 MB unpacked; the Linux one is 485 MB).
- [ ] **Step 4:** Commit.

### Task 4: The pipeline and launcher on Windows

**Files:**
- Modify: `app/src/setup_paths.cpp` (`DataLayout::Default`: `%LOCALAPPDATA%\kartrecomp` for data and cache, `%APPDATA%\kartrecomp` for config; `BuilderPaths::Detect`: `.exe` names, install-folder layout), `app/src/setup_pipeline.cpp` and `app/src/build_cli.cpp` (available RAM via `GlobalMemoryStatusEx`), `app/src/build_runner.cpp` (`DieWithParent`: a Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`), `app/src/setup_paths.cpp` `PlaceFile` (symlink needs privilege: try it, then a hard link on the same volume, then copy with the free-space check), `app/src/verify_build.cpp` (engine `.exe`, env), `app/src/game_package.cpp`
- Test: `tests/test_platform_paths.cpp` (Qt Test, runs on Linux and Windows), existing `ctest` suite on Windows CI

**Interfaces:**
- Produces: `qint64 AvailableMemoryBytes()` (in `build_runner.h`), `DataLayout::Default()` per platform, `PlaceFile` fallbacks; tests that need `/bin/sh` fake tools are skipped on Windows (`QSKIP`) and replaced by `.cmd` fixtures where the behaviour matters.

- [ ] **Step 1:** Write `tests/test_platform_paths.cpp`: `AvailableMemoryBytes() > 0`; `DataLayout::Default()` honours `LOCALAPPDATA`/`APPDATA` on Windows and the XDG variables on Linux (set through `qputenv`); a path with spaces and non-ASCII letters round-trips through `InstallInputs` using a copy fallback when symlinks fail (force the fallback with a test hook). Run on Linux and Windows CI; expected FAIL (functions missing).
- [ ] **Step 2:** Implement the Windows branches; add the Job Object and verify it with a test that starts a long-running child through `DieWithParent`, closes the job, and expects the child gone.
- [ ] **Step 3:** Run the whole `ctest` on Windows CI; expected PASS (skipped tests listed in the log).
- [ ] **Step 4:** Commit.

### Task 5: FSR 3 and DLSS plugins against the current Windows SDKs

**Files:**
- Create: `docs/WINDOWS_PLUGINS.md` (research result: which SDK versions and Vulkan backends exist today for AMD FidelityFX on Windows and NVIDIA NGX/Streamline; any Windows-only newer upscaler)
- Create: `plugins/fsr3/build-windows.ps1`, `plugins/dlss/build-windows.ps1`
- Modify: `plugins/fsr3/*.cpp`, `plugins/dlss/*.cpp` only where the Windows SDK API differs from the Linux one
- Test: `tests/windows/test_plugins.ps1`

**Interfaces:**
- Produces: `plugins\kr_upscaler_fsr3.dll` and `plugins\kr_framegen_fsr3.dll` (FSR 3 upscaling and frame generation, redistributable under MIT), `plugins\kr_upscaler_dlss.dll` and `kr_framegen_dlss.dll` (built on CI, shipped WITHOUT NVIDIA's runtime libraries), each exporting `kr_upscaler_get_api` / `kr_framegen_get_api` with the ABI in `native_gpu/upscaler.h`.

- [ ] **Step 1:** Research (fetch the vendors' current release notes and READMEs): write `docs/WINDOWS_PLUGINS.md` with versions, license terms for redistribution, Vulkan support, and any newer upscaler that exists only on Windows. If one exists, stop and ask the user whether to build it, before Step 3.
- [ ] **Step 2:** Write `tests/windows/test_plugins.ps1`: loads every DLL in `plugins\` with `LoadLibrary`, resolves the `get_api` export, checks the ABI version it reports, and (FSR 3) checks `create` returns null cleanly without a GPU instance. Run on CI; expected FAIL (no plugins).
- [ ] **Step 3:** Implement the build scripts and the minimal source changes; CI fetches the SDKs (never committed).
- [ ] **Step 4:** Run the test on CI; expected PASS. The real behaviour on a GPU is verified in Task 7.
- [ ] **Step 5:** Commit.

### Task 6: The Windows release zip

**Files:**
- Create: `.github/workflows/windows-release.yml` (on release published and `workflow_dispatch`; supersedes `windows-launcher.yml`)
- Create: `packaging/windows/assemble.ps1` (builds the zip: `KartRecomp\kartrecomp.exe`, `bin\` tools and DLLs, `engine\`, `toolchain\`, `plugins\`, `data\mk8d-3.0.4\hot_blocks.txt`; refuses to zip anything game-derived)
- Test: `tests/windows/test_release_contents.ps1`

**Interfaces:**
- Produces: `KartRecomp-windows-x64.zip` plus a `.sha256` file attached to the release; the updater asset name for Windows stays the release page (no self-replace on Windows).

- [ ] **Step 1:** Write `tests/windows/test_release_contents.ps1` (twin of `tests/test_release_contents.sh`): FSR 3 plugins present, no DLSS/NGX libraries, no `.nsp/.nca/.keys/.nso/recompiled_*` files, `ENGINE_VERSION` present, every `.exe`/`.dll` resolves its imports (`llvm-objdump -p` import check against the bundled DLLs plus system DLLs). Run on CI against an empty folder; expected FAIL.
- [ ] **Step 2:** Implement `assemble.ps1` and the workflow (windeployqt for the launcher, vcpkg DLLs for the engine).
- [ ] **Step 3:** Run on CI and download the artifact; expected PASS. Attach to the release only when the user says so.
- [ ] **Step 4:** Commit.

### Task 7: Acceptance on the user's Windows PC, then the USB drive

**Files:**
- Create: `tests/acceptance-windows.ps1` (twin of `tests/acceptance-linux.sh`: fresh profile folders via `LOCALAPPDATA`/`APPDATA`, only the zip and the user's files, runs `kartrecomp-setup.exe`, plays 60 s, reports build time, fps, plugin list, and whether anything outside the profile was touched)
- Create: `docs/WINDOWS.md` (how to run it, what to send back)

**Interfaces:**
- Consumes: the zip from Task 6.

- [ ] **Step 1:** Write the script and `docs/WINDOWS.md`; run it on CI with the real tools but no game files to prove it fails with the "pick your files" message (expected: exit 1 with that text).
- [ ] **Step 2:** Copy the zip and the script to the USB drive "racing 8 testing" (`/run/media/carter/racing 8 testing`), with a README listing the exact steps; confirm with `ls` and a checksum. The user runs it on their Windows PC.
- [ ] **Step 3:** Receive the user's report (log + numbers); fix what it shows (new tasks only if the findings are large); when the acceptance passes, tell the user the Windows build is functional (this is what unblocks merging).
