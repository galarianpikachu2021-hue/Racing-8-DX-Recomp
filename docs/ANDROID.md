# Android port: how to build, run and test (plan 1)

> **UNTESTED ON REAL HARDWARE.** This branch has only ever run in the Android emulator (x86_64, API 35). The arm64 build for Snapdragon
> devices (AYN Thor and others) compiles and links, but nobody has run it on a device yet: expect crashes, missing Adreno driver
> handling and unknown performance. There is no launcher yet and no dual-screen yet. Experimental, for developers.

Status (2026-10-23): the statically recompiled game runs in the fork's Android app in the emulator (API 35 image): main menu and a scripted race
at the Thor's handheld resolution, rendered by the native Vulkan renderer on the activity surface. Next: device readiness for Adreno
(`docs/superpowers/plans/2026-10-23-android-device-readiness.md`), then device testing on the Thor. The arm64 build exists (404 MB APK, `libyuzu-android.so` aarch64 with 1.26 M recompiled blocks, no JIT code).
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
# arm64 needs RECOMP_JOBS=1 (hot recomp units peak at 6-7 GB of clang each) and KARTRECOMP_HOST_JOBS=2 on a 15 GB PC; about 1.5 h from scratch
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

The default AVD is `kart_thor35` (API 35 image): its gfxstream driver exposes dynamic rendering as core. The API 33 AVD (`kart_thor`) does not
(the renderer logs `device ... dynamic rendering: none` and the boot stops cleanly, which is the intended behaviour for a GPU that lacks a feature).
No render-pass fallback was written: the Thor's Adreno/Turnip drivers have dynamic rendering.

## Findings

- The "guest PC 0 at about 12 s" crash and the title screen that never advanced were the same cause: the app's NAND had **no firmware** (the app creates
  `nand/system/Contents/registered` empty on its first start, and a copy step that only looked for the folder skipped it). Without the system
  archives the game's material/shader setup got null objects. With the firmware copied in, the keys must be loaded before the file-system
  factories scan it (`KeyManager::ReloadKeys()` in `EmulationSession::InitializeSystem`, else all-zero header key and a divide-by-zero while decrypting
  the firmware NCAs). Result: main menu at 60 fps, a scripted Grand Prix runs to lap 2/3, `static -> JIT 0`, no killed threads. Firmware is therefore a
  required input on Android (the launcher must copy it; `boot-check.sh` does it for tests).
- Handheld mode: the game draws 1280x720 into the 1920x1080 window textures and sets a crop (`nvnWindowSetCrop`); the renderer honours it
  (`OnWindowCrop` / `CropToPicture`). A rotated surface (the emulator's natively portrait panel) is presented with the identity pre-transform.
- Emulator numbers (6 GB, API 35, host GPU through gfxstream, handheld): 28-30 fps in the race, 4.5 GB peak RSS. Not the Thor's: performance waits for the device.
- The stock app pauses emulation when it goes to the background and waits for Resume; a replaced surface is handled in the renderer
  (`NativeSurface::Generation`) but only exercised by a display resize so far.

## What Eden's APK (v0.2.1, arm64) taught us (2026-10-23)

Unpacked read-only from `~/Downloads/Eden-Android-v0.2.1-standard.apk` (nothing run; Eden is GPL, our fork shares its lineage, so most of this is
already in our tree, but in the STOCK renderer). The point: the stock renderer's Adreno/Turnip machinery is not used by our native renderer.

- **Driver loading** (`EmulationSession::InitializeGpuDriver`, arm64 only): `adrenotools_open_libvulkan` opens either the system driver through
  a linker-namespace bypass or a user-chosen custom driver (Turnip), with hook libs (`libhook_impl.so`, `libgsl_alloc_hook.so`,
  `libfile_redirect_hook.so`, `libmain_hook.so`). The result is `m_vulkan_library`. Our `NativeSurface` loads `libvulkan.so` itself instead: it must use
  `m_vulkan_library` or Turnip and the driver picker do nothing.
- **BCn textures on Adreno**: Qualcomm's stock driver does not support BC formats; `adrenotools_get_bcn_type` says patchable / blob (native) /
  no, `adrenotools_patch_bcn(vkGetPhysicalDeviceFormatProperties)` patches the driver (API 28+, option `patch_old_qcom_drivers`). Turnip
  supports BC natively. MK8D's textures are mostly BCn: the native renderer needs the patch or a decode fallback (`VideoCommon::DecompressBCn`).
- **Qualcomm quirk list** (`vulkan_device.cpp`): broken custom border color, border-color swizzle, color write enable, shader float controls,
  shader atomic int64, 8/16-bit storage access; scaled vertex formats need emulation. Samsung: broken extendedDynamicState3ColorBlendEquation.
  Broken `VK_EXT_vertex_input_dynamic_state` on some drivers.
- **GPU turbo**: `adrenotools_set_turbo(true)` (`vk_turbo_mode.cpp`) raises the GPU clock; Eden exposes `turbo_speed_limit`.
- **Freedreno env** (`native_freedreno.cpp`): `TU_DEBUG`, `FD_MESA_DEBUG`, `FD_DEV_FEATURES` set before the driver loads (Turnip tuning without code).
- **Settings worth knowing**: `gpu_unswizzle_*` (texture unswizzle on the GPU instead of the CPU), `vram_usage_mode`, `astc_recompression`, `fast_gpu_time`,
  `async_presentation`, `dma_accuracy`, `use_asynchronous_shaders`. The EmuReady Thor reports run with "Accuracy: Performance", "DMA: Unsafe",
  "Vertex Input Dynamic State: on".
- **Audio**: oboe with per-chip quirks (Qualcomm MMAP, Samsung Exynos); already in our tree.
- **Our gap found with this**: `core/nvn_host/shader_translate.cpp` `DesktopProfile()` hardcodes float64/int64/int8/int16/float16/float controls = true,
  and the backend enables every device feature. Adreno has no fp64 hardware; Qualcomm's int64 atomics, 8/16-bit storage and float controls are
  broken. The translation profile must come from the device (plus the quirk table).

## Device checklist (Thor)

Performance mode; Turnip vs stock driver via the app's driver picker; scenarios: first track single player, effects race (drifts),
2-player split-screen; each at 720p and 1080p; record fps, peak RSS (`dumpsys meminfo`), compare with the EmuReady Eden reports
(55-60 fps single-player in Performance mode).
