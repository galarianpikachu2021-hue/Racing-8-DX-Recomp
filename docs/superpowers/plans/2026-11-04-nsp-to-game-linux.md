# NSP-to-game builder, Linux end to end: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On Linux, a user with their own base NSP, 3.0.4 update NSP, keys and firmware opens the launcher (AppImage), picks the files, presses Build, and gets a playable no-JIT game with every launcher tab working, with nothing installed or downloaded.

**Architecture:** A prebuilt engine (`mk8d-engine`, the fork's runtime with no game code) loads a per-user game module (`.so` files built from the exporter's C by a bundled clang) through the module-loading path the fork already has for Windows (`recompiled_*.dll` next to the exe: `recomp_image_lookup/set_base/run_slice/abi`), mirrored with `dlopen` on Linux. A headless exporter (extracted from the suyu GUI's export code) turns the NSPs into C; a C++ build runner inside the launcher splits units, schedules clang jobs by RAM, resumes, and verifies. Windows is a second plan, started when this one passes its acceptance run.

**Tech Stack:** C++20 (fork + Qt 6 launcher), Qt Test, CMake/Ninja, clang + lld (LLVM) bundled, linuxdeploy/appimagetool, the existing scripted-race benchmark (`benchmark-performance` skill, `tools/profile-report.py`).

**Spec:** `docs/superpowers/specs/2026-11-04-nsp-to-game-builder-design.md`

## Global Constraints

- Strict no JIT, every platform, forever: a missing block is a bug, never a fallback. The module build must keep `recomp_coverage.txt` = `fully static`.
- Speed: **no slower than today**, hard. Baseline = the installed unhooked tiered build, mods off (`KARTRECOMP_NO_MODS=1`), scripted effects race: mean core-0 samples about 43,300 (noise ±3%).
- Nothing derived from the game (NSPs, keys, firmware, generated C, objects, modules, the built game) is committed or distributed; user data lives in XDG folders (`~/.local/share/kartrecomp`, `~/.config/kartrecomp`, `~/.cache/kartrecomp`).
- Only update 3.0.4 (version 20<<16) is accepted; 3.0.3 and 1.0.0 are 32-bit and rejected with the reason.
- Module is the **unhooked** build (hooks opt-in at export); mods stay data-only; CTGP stays paused.
- Generated code targets x86-64-v3, never `-march=native`; tiered build: hot blocks `-O2`, cold `-O0`, 5000-block units, clang, 2 GB RAM per job assumed (measured peak 0.4 GB).
- Minimum advertised: 8 GB RAM and 4 threads; below that runs with fewer jobs and says so; refuse only when free RAM is under about 3 GB.
- Toolchain: glibc, dynamic, C-only module (no C++ ABI crossing), bundled sysroot; the user's distro needs no dev packages.
- Heavy jobs under `systemd-run --user --scope -p MemoryMax=...`, `pgrep -x mk8d` first, one game instance, back up `user/config/sdl2-config.ini` and `kartrecomp_overlay.ini` around tests, never `pkill -f`.
- Upstream mk8-recomp has no license: reference, never copy. Commits use the GitHub noreply email; nothing is pushed without the user saying so.

## Review Focus

- Wrong update (3.0.3, 1.0.0, 4.0.1) or a base NSP from another game: rejected on the Setup page with the reason, before any build starts.
- Missing or partial `prod.keys` (no `header_key`/master key for the update's generation): says which key is missing, no crash, no empty build.
- Cancel mid-build, then close and reopen the launcher: build resumes from finished units; a half-written object is never trusted.
- Low RAM or full disk mid-build: fewer jobs / a clear "out of space, need N GB" message, never a silent partial game.
- A module whose ABI or engine version differs from the engine: refused with "rebuild the game", never loaded.
- Paths with spaces and non-ASCII characters in the user's data folders and NSP locations.
- Play pressed while a build is half done: disabled, never starts a stale game.

---

### Task 1: Spike, module speed and memory against the static build

**Files:**
- Modify: `third_party/suyu/src/suyu_cmd/suyu.cpp` (Linux `dlopen` loader mirroring the `_WIN32` block at ~1566-1620: `recompiled_*.so` next to the exe; same symbols and ABI check `accept_recomp_abi`)
- Create: `tools/spike/module-vs-static.sh` (builds the module variant of the existing `local/exefs-304b-tiered3` tree into `local/spike/`, runs both scripted effects races, prints core-0 sample means)
- Doc: `docs/PIPELINE.md` (spike result section)

**Interfaces:**
- Produces: `LoadRecompModules(const std::filesystem::path& dir) -> std::vector<LoadedModule>` shared by Linux and Windows loaders, with `struct LoadedModule { void* handle; Core::RecompBlockFn lookup, run_slice; u64 base; unsigned abi; }`.

- [ ] **Step 1:** Write `tools/spike/module-vs-static.sh` so it fails when the module build is missing (`recompiled_image.so` absent) and prints `mean core-0 samples` for static and module runs (3 runs each, `KARTRECOMP_NO_MODS=1`).
- [ ] **Step 2:** Run it; expected FAIL: no module build.
- [ ] **Step 3:** Implement the dlopen loader in `suyu.cpp` by moving the Windows loop into `LoadRecompModules` with a `#ifdef _WIN32` loader abstraction (`LoadModuleHandle`, `FindSymbol`); `-fPIC`, `-fvisibility=hidden` except the five exported symbols, `-Wl,-z,now`; build the tree with `RECOMP_STATIC_ONLY=OFF`.
- [ ] **Step 4:** Run the script. Pass = module mean ≤ static mean +3% (the noise band), `fully static`, 60 fps in the plain race, sub-58-fps seconds not worse. If it fails, apply the hot-path fix from the spec (compile the dispatch/helper code into each module; engine stays prebuilt) and rerun until it passes; record the numbers in `docs/PIPELINE.md`.
- [ ] **Step 5:** Commit (`spike: module build matches the static build`).

### Task 2: Spike, does a fresh export reproduce today's tree

**Files:**
- Create: `tools/spike/compare-export.py` (compares two export trees after splitting: file lists, block counts per module, hash of each block function body)
- Doc: `docs/PIPELINE.md`

**Interfaces:**
- Produces: a report (`local/spike/export-diff.txt`) naming every difference between a fresh export and `local/exefs-304b-tiered3` (hooks, chaining, cold attribute, `-march`), and a decision line: `generator-clean` or `generator-fix-needed: <list>`.

- [ ] **Step 1:** Write the failing check: `compare-export.py <fresh> <reference>` exits 1 on any block-body difference; run it on a deliberately modified copy to prove it fails.
- [ ] **Step 2:** Make one fresh export from the user's files with the current fork (GUI + MCP path as in PIPELINE.md, `SUYU_AOT_TRANSLATE_ALL=1`, no block hooks), then `split_export.py --hot=data/mk8d-3.0.4/hot_blocks.txt --strip-hooks`.
- [ ] **Step 3:** Run the comparison; record the result. If `generator-fix-needed`, add the generator fixes (`core/recompiler/arm64_to_c.h`) as sub-steps before Task 3 (chaining via `ChainTo`/`RECOMP_TAIL`, `recomp_unhandled` cold attribute, `-ffunction-sections`), each with the same comparison going green.
- [ ] **Step 4:** Commit the tool and the recorded result.

### Task 3: Headless exporter `kartrecomp-export`

**Files:**
- Create: `third_party/suyu/src/suyu_cmd/export_main.cpp` (new target `kartrecomp-export`), extracted from `suyu/game_export.cpp` (`PrepareAotSourcePackage`, update handling) and `suyu/main.cpp:OnExportRecompiledSource`/`EmitProject`
- Modify: `third_party/suyu/src/suyu_cmd/CMakeLists.txt`
- Test: `tools/spike/export-smoke.sh`

**Interfaces:**
- Produces CLI: `kartrecomp-export --base <base.nsp> --update <update.nsp> [--dlc <x.nsp>...] --keys <prod.keys> --firmware <dir|zip> --out <dir> [--progress-fd <n>]`; writes the exefs C tree (`rtld/main/subsdk0/sdk`, `recomp_registration.c`, `recomp_abi_v4.h`, no block hooks) to `--out`; progress lines `PROGRESS <0-100> <stage text>` on `--progress-fd`; exit 0 ok, 2 bad input (message on stderr), 3 export failed.
- Consumes: Task 2's decision (generator state).

- [ ] **Step 1:** Write `export-smoke.sh`: run the exporter on the user's real files into a temp dir; assert exit 0, `recomp_registration.c` exists, four module folders exist; run it with a 3.0.3 update and assert exit 2 and a message mentioning 32-bit. Run it; expected FAIL: no such binary.
- [ ] **Step 2:** Implement `kartrecomp-export` with no Qt and no NAND profile: open NSPs with the fork's `FileSys::NSP`, apply the update through the explicit-content path the static build already uses, load keys/firmware via `KeyManager`, set `SUYU_AOT_TRANSLATE_ALL=1` internally, run the translation, call the shared emitter.
- [ ] **Step 3:** Run `export-smoke.sh`; expected PASS; also run `compare-export.py` against the reference tree: `generator-clean`.
- [ ] **Step 4:** Commit; re-export fork patches (`patches/suyu/0001`-`0003`).

### Task 4: Unit splitter and compile scheduler in C++ (`BuildRunner`)

**Files:**
- Create: `app/src/build_runner.h`, `app/src/build_runner.cpp`, `app/src/unit_split.h`, `app/src/unit_split.cpp`
- Create: `tests/CMakeLists.txt`, `tests/test_unit_split.cpp`, `tests/test_build_runner.cpp` (Qt Test); modify root `CMakeLists.txt` to `add_subdirectory(tests)`, `enable_testing()`
- Reference (not copied at runtime): `tools/recomp/split_units.py`, `split_export.py`

**Interfaces:**
- Produces: `struct BuildPlan { QString exportDir, outDir, clang, sysroot; int jobs; }`; `QList<Unit> SplitUnits(const QString& moduleDir, const QSet<quint64>& hot, int blocksPerUnit = 5000)`; `class BuildRunner : QObject { void start(BuildPlan); void cancel(); signals: progress(int done, int total, QString unit), finished(bool ok, QString message) }`; `int JobsForRam(qint64 availableBytes, int threads)` (2 GB per job, at least 1, at most `threads`; returns 0 below 3 GB meaning refuse); `bool HasDiskSpace(const QString& dir, qint64 neededBytes)` (build needs about 3 GB for objects; `BuildRunner` finishes with `ok=false` and the message "need N GB free in <dir>" when short).
- Consumes: Task 3's output tree; `data/mk8d-3.0.4/hot_blocks.txt`.

- [ ] **Step 1:** Write failing tests: `JobsForRam(16 GiB, 8) == 8`, `JobsForRam(8 GiB, 8) == 4`, `JobsForRam(2.5 GiB, 8) == 0`; `SplitUnits` on a fixture module of 12,000 blocks gives units of ≤5000 blocks, hot blocks in units flagged `-O2`, cold `-O0`; a resumed run skips units whose `.o` and `.done` marker exist and recompiles one with an `.o` but no marker; `HasDiskSpace` is false for a 10 TB request and the runner stops with the free-space message before compiling.
- [ ] **Step 2:** Run `ctest -R unit_split` and `-R build_runner`; expected FAIL.
- [ ] **Step 3:** Implement. Splitting output must be byte-identical to `split_export.py` on the real tree (add a test comparing against a stored list of unit file names and block counts, not game code). Compile jobs run `clang -O2/-O0 -march=x86-64-v3 -fPIC -ffunction-sections -foptimize-sibling-calls` per unit through `QProcess`, markers written after a successful compile (write temp + rename), progress = finished/total.
- [ ] **Step 4:** Run tests; expected PASS. Then run `BuildRunner` on the real tree under `systemd-run --scope -p MemoryMax=14G` and confirm the module links and passes Task 1's checks.
- [ ] **Step 5:** Commit.

### Task 5: Engine release build and module ABI/version guard

**Files:**
- Modify: `third_party/suyu/src/suyu_cmd/suyu.cpp` (engine refuses modules with another `recomp_image_abi` or engine-version string; message names both)
- Create: `packaging/build-engine.sh` (builds `mk8d-engine` from the fork with `-DSUYU_NO_JIT=ON`, no recomp dir, plus plugins; strips; writes `engine/ENGINE_VERSION`)
- Test: `tests/test_module_guard.sh` (runs the engine against a stub module with a wrong ABI; expects exit code 4 and the message)

**Interfaces:**
- Produces: `engine/mk8d-engine`, `engine/plugins/*.so`, `engine/ENGINE_VERSION` (`<fork commit>-abi<n>`); module folder stamp `module.json {engineVersion, exportHash, unitCount}` written by BuildRunner after a verified build.
- Consumes: Task 1's `LoadRecompModules`.

- [ ] **Step 1:** Write `test_module_guard.sh` and run it; expected FAIL (no guard).
- [ ] **Step 2:** Implement the guard and `build-engine.sh`; the engine must start with no module present and print a clear "no game built yet" message (exit 5).
- [ ] **Step 3:** Run the test; expected PASS. Run the full unhooked race with the engine + the Task 4 module: `fully static`, 60 fps.
- [ ] **Step 4:** Commit.

### Task 6: Bundled toolchain (clang, lld, sysroot)

**Files:**
- Create: `packaging/make-toolchain.sh` (assembles `toolchain/` from a pinned LLVM release tarball (fetched at packaging time, checksum pinned in the script) and a minimal glibc sysroot built from an older-base container image or distro package set: C headers, `libc`, `libm`, crt files)
- Test: `tests/test_toolchain.sh` (compiles and links a unit from the real tree with `PATH=/nonexistent`, no system compiler, and checks the object runs inside the engine)

**Interfaces:**
- Produces: `toolchain/bin/clang`, `toolchain/bin/ld.lld`, `toolchain/sysroot/`, `toolchain/TOOLCHAIN_VERSION`; BuildRunner passes `--sysroot=<toolchain>/sysroot -fuse-ld=lld --target=x86_64-linux-gnu`.

- [ ] **Step 1:** Write `test_toolchain.sh`; expected FAIL (no toolchain).
- [ ] **Step 2:** Implement `make-toolchain.sh`; keep only what a C compile and link needs, report the size (target under 400 MB unpacked, measured).
- [ ] **Step 3:** Run the test inside `env -i` with no system compilers on PATH; expected PASS; run the whole tiered build with the bundled clang and compare module speed with Task 1 (clang version may differ: record it).
- [ ] **Step 4:** Commit.

### Task 7: Setup page, validators, Build and Play

**Files:**
- Create: `app/src/nsp_info.h`, `app/src/nsp_info.cpp` (C++ port of `tools/nsp-cnmt-version.py` plus the NPDM 64-bit check), `app/src/keys_check.{h,cpp}`, `app/src/setup_page.{h,cpp}`
- Modify: `app/src/main_window.cpp/.h` (new first tab "Setup"; Play enabled only when a verified module exists), `app/CMakeLists.txt`
- Test: `tests/test_nsp_info.cpp`, `tests/test_keys_check.cpp`

**Interfaces:**
- Produces: `struct NspInfo { quint64 titleId; quint32 version; bool aarch64; QString error; }`, `NspInfo ReadNspInfo(const QString& path, const QString& prodKeys)`; `enum class KeyProblem { None, NoFile, MissingHeaderKey, MissingMasterKey }`, `KeyProblem CheckKeys(const QString& prodKeys, int masterKeyGeneration)`; `QString ValidateInputs(const SetupInputs&)` returning "" or the user-facing reason; `struct SetupInputs { QString base, update, keys, firmware; QStringList dlc; }`.
- Consumes: `BuildRunner` (Task 4), toolchain and engine paths (Tasks 5-6).

- [ ] **Step 1:** Write failing tests with tiny synthetic NSP fixtures built in the test (no game data): update with version `20<<16` and AArch64 flag passes; `19<<16` (3.0.3) fails with "32-bit"; a base NSP whose title id is another game fails with the title id in the message; a keys file lacking `header_key` returns `MissingHeaderKey`; a path with spaces and non-ASCII characters opens.
- [ ] **Step 2:** Run `ctest -R nsp_info|keys_check`; expected FAIL.
- [ ] **Step 3:** Implement the validators and the Setup page (file pickers with tick/cross and the reason; firmware folder or zip; DLC list; Build button with progress bar from `BuildRunner::progress`, Cancel, "Resume" label when markers exist; Play enabled only when `module.json` matches the engine version and the inputs' hash).
- [ ] **Step 4:** Run tests; expected PASS. Launch the launcher offscreen (`QT_QPA_PLATFORM=offscreen`) and check the Setup page enables Build only with valid inputs.
- [ ] **Step 5:** Commit.

### Task 8: Verification step (Null-renderer boot)

**Files:**
- Create: `app/src/verify_build.{h,cpp}`
- Modify: `third_party/suyu/src/suyu_cmd/suyu.cpp` (`--verify-boot <seconds>`: runs headless with the Null renderer, exits 0 when `recomp_coverage.txt` says `fully static` and the title frame count is reached; exit 6 otherwise with the first problem on stderr)
- Test: `tests/test_verify_build.cpp`

**Interfaces:**
- Produces: `struct VerifyResult { bool ok; QString message; }`, `VerifyResult VerifyBuild(const QString& engine, const QString& moduleDir, const SetupInputs&)`; BuildRunner calls it last and writes `module.json` only on success.

- [ ] **Step 1:** Write failing tests: a stub engine script exiting 0 yields ok; exit 6 with a message yields not ok and no `module.json`.
- [ ] **Step 2:** Run `ctest -R verify_build`; expected FAIL.
- [ ] **Step 3:** Implement `--verify-boot` and `VerifyBuild`; real run on the user's files must pass in under 60 s with no window and no GPU.
- [ ] **Step 4:** Run tests and the real verification; expected PASS.
- [ ] **Step 5:** Commit.

### Task 9: Launcher settings route through the new layout

**Files:**
- Modify: `app/src/game_package.cpp/.h`, `app/src/main_window.cpp` (all tabs read/write the XDG data/config folders, launch `engine/mk8d-engine` with `--module-dir`, same `launch.env` and overlay ini handling the AppRun uses today), `packaging/AppRun`
- Test: `tests/test_game_package.cpp`

**Interfaces:**
- Produces: `GamePackage::fromXdg()` (paths below) and `QStringList GamePackage::launchArgs() const`; layout: `~/.local/share/kartrecomp/{content/{base.nsp,update-3.0.4.nsp,dlc/},keys/,nand/system/,mods/,module/,log/}`, `~/.config/kartrecomp/{sdl2-config.ini,kartrecomp_overlay.ini,launch.env}`.

- [ ] **Step 1:** Write failing tests: `fromXdg()` honours `XDG_DATA_HOME`/`XDG_CONFIG_HOME` with spaces in them; `launchArgs()` lists base, update and each DLC; Enhancements settings still land in `kartrecomp_overlay.ini`.
- [ ] **Step 2:** Run `ctest -R game_package`; expected FAIL.
- [ ] **Step 3:** Implement; copy (not link) the picked NSPs/keys/firmware into the data folder so the build does not depend on where they were; the developer layout (`local/game`) keeps working through the existing `findDefaultRoot`.
- [ ] **Step 4:** Run tests; then click through every tab against the built game and confirm each setting is read by the game (resolution, render scale, upscaler, HDR, controllers, mods, DLC).
- [ ] **Step 5:** Commit.

### Task 10: One AppImage with everything, and the clean-machine acceptance run

**Files:**
- Create: `packaging/make-release-appimage.sh` (launcher + `engine/` + exporter + `toolchain/` + `data/`; no game, no generated files)
- Modify: `.github/workflows/` (Linux release job later; not pushed until the user says)
- Doc: `docs/PIPELINE.md`, `README.md` (how it works now), `docs/superpowers/plans/` (Windows plan stub)

**Interfaces:**
- Produces: `KartRecomp-x86_64.AppImage` (launcher mode by default, size measured and recorded; target under 1 GB).

- [ ] **Step 1:** Write the acceptance script `tests/acceptance-linux.sh`: fresh `HOME` and XDG folders, a container or `env -i` shell with no compilers, runs the AppImage's CLI build mode with the user's files, then `--verify-boot`; fails if anything outside the AppImage and the user's files is touched.
- [ ] **Step 2:** Run it; expected FAIL (no AppImage).
- [ ] **Step 3:** Implement `make-release-appimage.sh`; check the AppImage contains no `.nsp`, `.keys`, firmware, generated C or objects (`find` + name list in the script).
- [ ] **Step 4:** Run the acceptance script on a clean `HOME`; expected PASS: build in minutes, verified, Play launches the game to the main menu at 60 fps, and the speed numbers match Task 1.
- [ ] **Step 5:** Commit; write the Windows plan stub (MinGW target, Windows engine, `recompiled_*.dll` path already in the fork, FSR 3 / DLSS Windows SDK plugins) as the next plan.
