# Android Engine Bring-up Implementation Plan (plan 1 of 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The static (no-JIT) recompiled game boots to the title screen and races inside the fork's Android app, first in the
x86_64 `thor_api33` emulator, with an arm64-v8a build that compiles and is ready for the Thor.

**Architecture:** Reuse the fork's existing Android app (`src/android`, yuzu/suyu lineage: Kotlin UI + `libyuzu-android.so` via
JNI, `chromeOS` flavor = x86_64, `mainline` = arm64). Add a "static recomp" build mode to its native CMake that links the
generated game code (tiered tree, clang) and the strict static CPU backend, add an explicit base+update boot entry, and give the
native Vulkan backend (`suyu_cmd/native_gpu`) an `ANativeWindow` surface instead of SDL. The launcher (plan 2) and dual screen
(plan 3) come later; this plan uses the stock app UI only to start the game.

**Tech Stack:** Android SDK 36 + NDK 28.2.13676358 (the app's pinned versions), Gradle (app's wrapper), JDK 17, CMake/Ninja, NDK
clang for the generated C, adb + the Android emulator (`thor_api33`, x86_64, display 0 + a 1240x1080 second display).

**Spec:** `docs/superpowers/specs/2026-10-23-android-dual-screen-design.md`

## Global Constraints

- Static, strict, no JIT: `SUYU_NO_JIT=ON`, `SUYU_CMD_STATIC_RECOMP_STRICT` behaviour; a missing block is a crash, never a JIT fallback.
- Game version 3.0.4 only (base + update NSP supplied by the player); nothing game-derived in the repo or the APK.
- Generated code: tiered build (hot `-O2`, cold `-O0`, `data/mk8d-3.0.4/hot_blocks.txt`), 5000-block units, x86_64 emulator build
  `-march=x86-64-v3` ceiling; arm64 build `-march=armv8.2-a` (Snapdragon 8-class baseline) at most.
- Tight RAM (base-model Thor): record peak RSS of every run; no step may raise it without saying so in the commit.
- Desktop builds stay RAM-capped (`systemd-run --user --scope -p MemoryMax=8G`, `nice -n 19`), one heavy build at a time, check `free -m` first.
- Toolchain lives in `/home/carter/Claude Projects/toolchain` (shared with the user's other Android project); add versions there with
  `sdkmanager`, never remove what is installed.
- Push to GitHub only when the user asks.

## Review Focus

- **Emulator GPU without the features the renderer assumes** (no BC texture compression, no `VK_EXT_*` the desktop path uses): the
  app must log which feature is missing and stop, not crash or hang. Test in Task 5.
- **Content picked through Android's document picker** (a `content://` URI, not a file path): boot must work from an FD/URI. Test in Task 4.
- **App sent to background and resumed mid-race** (surface destroyed and recreated): no crash; rendering resumes. Test in Task 6.
- **Low memory** (the emulator set to 6 GB, a base-model-like budget): boots to the title screen; peak RSS printed. Test in Task 6.
- **Stock Qualcomm driver vs Turnip** on device: the app's existing driver selection (`native_freedreno.cpp`, libadrenotools) keeps
  working with the native backend. Covered only on the device (documented check in Task 7).

---

### Task 1: Toolchain for the fork's Android app

**Files:**
- Create: `tools/android/env.sh` (repo; sources the user's toolchain dir, sets `ANDROID_NDK_HOME` to 28.2.13676358)
- Create: `tools/android/check-toolchain.sh`

**Interfaces:**
- Produces: `source tools/android/env.sh` puts `adb`, `emulator`, `sdkmanager`, JDK 17 on PATH and exports `ANDROID_HOME`,
  `ANDROID_NDK_HOME`, `ANDROID_AVD_HOME`, `GRADLE_USER_HOME` (paths from `toolchain/env.sh`, NDK version changed).

- [ ] **Step 1: Write the check script** `tools/android/check-toolchain.sh`: exits 0 only if `platforms;android-36`,
  `ndk;28.2.13676358`, a build-tools ≥ 36 and the `thor_api33` AVD exist; prints the missing ones.
- [ ] **Step 2: Run it.** Expected now: FAIL listing `android-36`, `ndk 28.2.13676358`, build-tools 36.
- [ ] **Step 3: Install** with `sdkmanager "platforms;android-36" "ndk;28.2.13676358" "build-tools;36.0.0"` (toolchain dir, no sudo).
- [ ] **Step 4: Run the check again.** Expected: PASS.
- [ ] **Step 5: Commit** `tools/android/` ("android: toolchain env and check").

### Task 2: Stock fork app builds and starts in the emulator

**Files:**
- Create: `tools/android/build-app.sh <flavor> <buildtype>` (runs the app's `gradlew` from `src/android` with `env.sh`, copies the APK to `local/android/`)
- Create: `tools/android/emu.sh start|stop|install <apk>|logcat` (headless `emulator -avd thor_api33 -no-window -gpu swiftshader_indirect`,
  waits for `sys.boot_completed`; `-gpu host` variant for speed checks)

**Interfaces:**
- Produces: `local/android/app-chromeOS-release.apk` (x86_64), `local/android/app-mainline-release.apk` (arm64) (unsigned/debug-signed
  for now; release signing is plan 2).

- [ ] **Step 1:** `build-app.sh chromeOS relWithDebInfo` with the fork as-is (JIT build, no recomp). Expected: APK produced. Fix only
  build breaks caused by our patches (record each fix in the commit).
- [ ] **Step 2:** `emu.sh start && emu.sh install local/android/app-chromeOS-relWithDebInfo.apk && adb shell am start -n <pkg>/<main activity>`.
  Expected: logcat shows the main activity resumed, no `FATAL EXCEPTION`.
- [ ] **Step 3: Commit** the scripts and any build fixes.

### Task 3: Static recomp build mode for the Android native library

**Files:**
- Modify: `src/android/app/src/main/jni/CMakeLists.txt` (fork) — option `KARTRECOMP_STATIC_RECOMP` (default OFF): when ON, require
  `SUYU_CMD_RECOMP_DIR`, add the recomp module libraries (`recomp_static_main/rtld/sdk/subsdk0`) the same way `src/suyu_cmd/CMakeLists.txt`
  does, define `SUYU_CMD_STATIC_RECOMP=1 SUYU_CMD_STATIC_RECOMP_STRICT=1`, link them into `yuzu-android`.
- Modify: `src/android/app/build.gradle.kts` (fork) — pass `-DKARTRECOMP_STATIC_RECOMP=ON -DSUYU_NO_JIT=ON -DSUYU_CMD_RECOMP_DIR=<path>
  -DCMAKE_C_COMPILER=clang` and the cold flags when the Gradle property `kartrecomp.recompDir` is set.
- Modify: `tools/android/build-app.sh` — `--recomp <exefs tree>` passes that property.

**Interfaces:**
- Consumes: the tiered tree `mk8-recomp/local/exefs-304b-tiered3` (made by `tools/recomp/split_export.py --hot=...`).
- Produces: `libyuzu-android.so` containing `blk_main_*` symbols and the static registry; APK `local/android/app-chromeOS-recomp.apk`.

- [ ] **Step 1: Failing check:** `nm -D --defined-only` of the built `libyuzu-android.so` (x86_64) must list `recomp_image_run_slice_main`
  (or the static registry symbol the desktop build exports). Expected now: missing.
- [ ] **Step 2:** implement the CMake option and Gradle wiring; build `chromeOS` with `--recomp`.
- [ ] **Step 3:** run the check. Expected: symbol present; record the `.so` size and the generated-code compile wall/CPU time from `.ninja_log`.
- [ ] **Step 4: Commit** (fork changes go into the patches with Task 7's re-export).

### Task 4: Explicit base + update boot on Android

**Files:**
- Modify: `src/android/app/src/main/jni/native.cpp` (fork) — JNI `bootExplicit(String baseUri, String updateUri, String[] dlcUris)`
  that opens each through the app's existing content-URI → FD path and boots with the explicit-content loader the desktop static build uses
  (patch 0001, `--content-base/--content-update/--content-dlc`); refuses (returns an error code) when the update is not 3.0.4.
- Modify: `src/android/app/src/main/java/.../NativeLibrary.kt` (fork) — `external fun bootExplicit(...)`: Int.
- Modify: the stock UI's game-start path minimally: a debug-only menu item "Boot recomp (pick base, pick update)".

**Interfaces:**
- Produces: `NativeLibrary.bootExplicit(baseUri: String, updateUri: String, dlcUris: Array<String>): Int` (0 = started; 1 = wrong update
  version; 2 = cannot open a file; 3 = keys/firmware missing).

- [ ] **Step 1: Failing check:** in the emulator, push base/update NSPs and keys+firmware into the emulator's shared storage (local copies,
  never the repo), call `bootExplicit` through the debug item with the two `content://` URIs. Expected now: method missing.
- [ ] **Step 2:** implement; build `chromeOS --recomp`; install.
- [ ] **Step 3:** run the check. Expected: logcat shows the guest main thread starting (the desktop log lines `ArmRecomp::RunThread entered`,
  `Early static dispatch #0`) and `fully static` coverage after 30 s; an NSP of another update version returns 1 with a toast.
- [ ] **Step 4: Commit.**

### Task 5: Native Vulkan backend on an Android surface

**Files:**
- Modify: `src/suyu_cmd/native_gpu/vk_shadow_backend.cpp` (fork) — surface creation behind a small interface: `VK_KHR_android_surface`
  from an `ANativeWindow*` when `__ANDROID__`, SDL otherwise; feature probe at startup that logs every required feature/extension and
  returns a clear error ("this GPU lacks X") instead of continuing.
- Create: `src/suyu_cmd/native_gpu/native_surface.h` — `struct NativeSurface { virtual VkSurfaceKHR Create(VkInstance) = 0; virtual void Size(uint32_t& w, uint32_t& h) = 0; };`
  with `SdlSurface` and `AndroidSurface(ANativeWindow*)`.
- Modify: `src/android/app/src/main/jni/CMakeLists.txt` — compile `suyu_cmd/native_gpu/*.cpp` and the nvn_host core into `yuzu-android`
  when `KARTRECOMP_STATIC_RECOMP`; `native.cpp` passes the activity's surface to `AndroidSurface` and sets `KARTRECOMP_NATIVE_GPU=native` behaviour.

**Interfaces:**
- Consumes: `bootExplicit` (Task 4).
- Produces: `NativeSurface` (used again by plan 3 for the second screen).

- [ ] **Step 1: Failing check:** boot in the emulator (`-gpu host`), then `adb exec-out screencap -p > local/android/shot-title.png` after 40 s.
  Expected now: black or the stock renderer.
- [ ] **Step 2:** implement; build; install.
- [ ] **Step 3:** run the check. Expected: the title screen in the screenshot; logcat `frame pacing` lines. With `-gpu swiftshader_indirect`
  either the title screen or one clear "missing feature" log line, never a crash or hang.
- [ ] **Step 4: Commit.**

### Task 6: Race, lifecycle and memory in the emulator

**Files:**
- Create: `tools/android/race-check.sh` — pushes a TAS script (made with `tools/make-menu-tas.py --centre-stick`) to the app's TAS dir,
  boots with TAS playback, screenshots at 90 s, pulls the log, prints average fps (PERF lines), peak RSS (`dumpsys meminfo <pkg>` sampled
  every 5 s) and the coverage line.
- Modify: `native.cpp` / the activity glue (fork) — surface destroyed → pause rendering and release the swapchain; surface created → recreate.

- [ ] **Step 1: Failing check:** `race-check.sh` (emulator RAM 6144 MB). Expected now: no race or no numbers.
- [ ] **Step 2:** TAS playback path working on Android (the app's existing TAS input code), lifecycle handling.
- [ ] **Step 3:** run the check. Expected: race screenshot, `fully static`, an fps number and a peak RSS number; then `adb shell input keyevent HOME`,
  wait 5 s, relaunch: rendering resumes, no crash in logcat.
- [ ] **Step 4:** write the numbers into `docs/ANDROID.md` (new: how to build, run, and the emulator baseline; emulator fps is not the Thor's).
- [ ] **Step 5: Commit.**

### Task 7: arm64 build for the Thor and patch export

**Files:**
- Modify: `tools/android/build-app.sh` — `mainline --recomp` builds the arm64 generated code with NDK clang `--target=aarch64-linux-android26
  -march=armv8.2-a`, tiered flags.
- Modify: `patches/suyu/0001-*.patch`, `0002-*.patch` (re-export; include `src/android`), `docs/ANDROID.md` (device checklist: Performance
  mode, Turnip vs stock driver via the app's driver picker, the predefined test set from the spec: first track, effects race, 2P, at 720p and
  1080p, peak RSS, compare with the EmuReady Eden numbers).

- [ ] **Step 1: Failing check:** `file` on the arm64 `libyuzu-android.so` from the mainline recomp APK. Expected now: no such APK.
- [ ] **Step 2:** build `mainline --recomp`.
- [ ] **Step 3:** run the check. Expected: `ELF 64-bit LSB shared object, ARM aarch64`, `blk_main_*` symbols present, APK size recorded.
  (Runs on the Thor only; not installed in the x86_64 emulator.)
- [ ] **Step 4:** re-export the fork patches; `git diff --stat patches/` shows the Android changes.
- [ ] **Step 5: Commit.**

---

Plans 2 (Android launcher + on-device build + Obtainium releases) and 3 (dual screen, desktop first) follow when this plan is done; the
on-device compiler (bundled clang inside the APK) belongs to plan 2, so plan 1 compiles the generated code on the desktop.
