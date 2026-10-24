# Android port: how to build, run and test (plan 1)

Status (2026-10-23): the statically recompiled game links into the fork's Android app and boots its guest in the emulator;
the native Vulkan renderer is wired to the activity surface but the emulator's GPU lacks a feature it needs (see "Emulator GPU").
Design: `docs/superpowers/specs/2026-10-23-android-dual-screen-design.md`. Plan: `docs/superpowers/plans/2026-10-23-android-engine-bringup.md`.

## Toolchain

`source tools/android/env.sh` (shared toolchain dir `~/Claude Projects/toolchain`, reached through a space-free link; NDK 28.2.13676358,
SDK 36, CMake 3.31.6, JDK 17). `tools/android/check-toolchain.sh` lists anything missing. `kr_sdkmanager` (defined by env.sh) replaces
`sdkmanager`, whose launcher breaks on the space in the path.

## Build

```
tools/android/build-app.sh chromeOS relWithDebInfo                      # stock app, x86_64 (emulator)
KARTRECOMP_BUILD_MEM=12G tools/android/build-app.sh chromeOS relWithDebInfo \
    --recomp <exefs tree from tools/recomp/split_export.py --hot=data/mk8d-3.0.4/hot_blocks.txt>   # with the recompiled game
tools/android/build-app.sh mainline relWithDebInfo --recomp <tree>      # arm64-v8a (the Thor), same flags
```

APKs land in `local/android/`. First recomp build: about 17 minutes wall on 8 cores (about 40 CPU-minutes for 284 generated units, clang
tiered), 8 GB is not enough (clang peaked at 9.5 GB before the cap killed it): use 12 GB. Library size with the game: 1.35 GB stripped.

## Emulator

`tools/android/emu.sh start [host|swiftshader] | stop | install <apk> | logcat`. AVD `kart_thor` (a copy of the user's `thor_api33`:
1920x1080 plus a 1240x1080 second display, 32 GB data); the window is visible unless `EMU_HEADLESS=1`. `EMU_RAM_MB` sets its RAM
(default 6144; base-model budget tests use less).

Checks: `tools/android/smoke.sh <apk>` (app starts), `check-recomp-lib.sh <apk>` (game linked in), `boot-check.sh <apk> <game dir>`
(copies base, update 3.0.4, keys and firmware from the local game folder into the app's storage once, boots with the explicit
content intent, passes when 4 static modules register and a guest thread runs; `SHOT_OUT=file.png` adds the rendering check; a third
argument boots a wrong update and passes when it is refused).

## How the game starts on Android

`EmulationActivity` is started with string extras `kartrecomp.base`, `kartrecomp.update` (and `kartrecomp.dlc` array): paths or content
URIs. `NativeLibrary.setExplicitContent` stores them, `native.cpp` registers the recompiled modules (`KartStaticRecomp::Register`,
shared with the desktop static build), configures the explicit update provider (private content provider, refuses anything but 3.0.4),
sets the emulated GPU to Null, starts the native renderer on the activity's `ANativeWindow` (`NativeSurface`), then loads.

## Emulator GPU

`-gpu host` shows the host GPU through gfxstream at Vulkan 1.3 but without dynamic rendering (core or KHR): the native renderer logs
`device ... dynamic rendering: none` / `device lacks dynamic rendering` and the boot stops cleanly. Options, in the order to try: a newer
system image (API 35/36), an opt-in non-presenting backend so the guest and memory can be tested in the emulator, a render-pass
fallback (only if the picture in the emulator is wanted). The Thor's Adreno/Turnip drivers have dynamic rendering.

## Findings

- Guest PC 0 at about 12 s in the emulator (null global function pointer loaded at `main+0x58fa0c`, GOT `0x1301008`): not yet explained;
  next check is the desktop static build with the emulated renderer set to Null and `KARTRECOMP_NATIVE_GPU` unset.
- Emulator fps is not the Thor's: performance work waits for the device (checklist in the plan's Task 7).

## Device checklist (Thor)

Performance mode; Turnip vs stock driver via the app's driver picker; scenarios: first track single player, effects race (drifts),
2-player split-screen; each at 720p and 1080p; record fps, peak RSS (`dumpsys meminfo`), compare with the EmuReady Eden reports
(55-60 fps single-player in Performance mode).
