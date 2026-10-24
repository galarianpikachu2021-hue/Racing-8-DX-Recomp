# Android device readiness for the native renderer (plan 1b)

> Follows `2026-10-23-android-engine-bringup.md` (done through Task 6). Found by reading Eden's APK (see docs/ANDROID.md, "What Eden's APK taught us").
> Goal: the native renderer is correct and fast on Adreno (Snapdragon 8 Gen 2 Thor) with Turnip or the stock driver. Everything here is
> verifiable on the desktop (RADV) by forcing quirks with an environment switch; only the speed numbers need the device.

**Spec:** `docs/superpowers/specs/2026-10-23-android-dual-screen-design.md`

Tasks (in order; each: failing check first, then change, then the desktop check that nothing regressed, then commit):

1. **Device capabilities and quirks** (`DeviceCaps`: vendor, driver version, fp64/int64/int8/int16/fp16/float controls/demote/etc. from the
   queried features; a quirk table: Qualcomm 0x5143 = no int64 atomics, no 8/16-bit storage access, no float controls, no fp64;
   `KARTRECOMP_QUIRKS=qualcomm` forces it on any GPU). `shader_translate.cpp` builds its `Shader::Profile` and `HostTranslateInfo` from `DeviceCaps`
   (set once by the backend before the first translation). Check: with the forced quirk the desktop renders the title screen and a race with
   0 failed pipelines (compare with the log's "pipelines (0 failed)").
2. **The app's Vulkan library** (arm64): `SetAndroidVulkanLibrary(handle)` from `EmulationSession::m_vulkan_library` into `AndroidSurface::Loader`
   (dlsym on that handle). Check: x86_64 emulator unchanged (handle null = old path); arm64 build compiles.
3. **BCn on Adreno**: query `vkGetPhysicalDeviceFormatProperties` for BC formats; with `adrenotools_get_bcn_type == PATCH` and API >= 28 call
   `adrenotools_patch_bcn` (honour the app's `patch_old_qcom_drivers`); if BC is still unsupported, decode BCn to RGBA8 with
   `VideoCommon::DecompressBCn` at upload. Check: desktop with `KARTRECOMP_QUIRKS=no-bc` (forces the decode path) renders the title screen
   identically (screenshot compare) and reports the decoded-texture counter.
4. **Turbo mode and Freedreno env**: `adrenotools_set_turbo(true)` while the renderer runs (setting `turbo`), the Turnip env keys come from the
   app's existing Freedreno settings (already applied before driver load; verify the log lines).
5. **Memory budget**: RAM per piece (guest memory, texture cache, ring buffers) logged at start-up and every 60 s as one line; texture cache
   cap derived from `VK_EXT_memory_budget` / total RAM (base-model Thor: 8 GB shared). Check: peak RSS in `race-check.sh` before/after.
6. **GPU unswizzle (investigate)**: measure CPU time of the texture upload/untile path in a race (profile numbers already in the log:
   "tex-check", "upload"); only if it is a top cost on the Thor, port Eden's compute unswizzle. Output: a measurement and a go/no-go note.
