# Racing 8 DX Recomp v0.2.0 (draft)

You bring your own game files; nothing from the game is in this release.

## New: easy install, no command line

The launcher now builds the game for you. Open the **Setup** tab, pick your own files, press **Build game**, then **Play**.

- Pick: base game `.nsp`, the 3.0.4 update `.nsp`, optional DLC `.nsp`, your `prod.keys`, and the firmware (a folder or the `.zip`).
- Every row shows a tick or a plain-language reason (wrong update version, missing key, and so on).
- The build takes about 5 minutes on an 8-thread PC (about 3 GB of free RAM and 7 GB of disk). **Cancel** at any point; **Build** resumes where it stopped.
- Your `.nsp` files are **moved** into the game's folder after a warning, so the game does not need the original files. Keys and firmware are copied.
- A tick on every row after a build; the Setup page shows what is installed even if its settings were reset.
- No JIT anywhere: the game is statically recompiled on your PC from your own files.

### Windows (first test release)

- Unzip and run `kartrecomp.exe`. Everything the program writes goes into a `userdata` folder beside it, so you can copy the whole folder to another PC.
- `Collect-Logs.cmd` packs only logs and settings (no game files or keys) into `kartrecomp-logs.zip` for bug reports.
- The Windows build uses a bundled compiler (llvm-mingw). It is unsigned, so SmartScreen will ask you to confirm.
- Upscaler plugins: FSR 3 and FSR 4 (AMD FidelityFX SDK 2.4, signed AMD DLLs shipped unmodified with their licence) are in the zip, with a **Model** row under Upscaler in the in-game menu (F10). FSR 4 picks its model per GPU (including the lightweight INT8 model); other GPUs fall back to FSR 3.1.
- DLSS is not included (NVIDIA's libraries cannot be redistributed); it can be built locally for your own use, see `docs/WINDOWS_PLUGINS.md`.
- Windows frame generation and some slowdowns are still being worked on; this is a test release for that.

## Fixed

- **Only a part of the picture was visible** (zoomed into the top-left) at any output resolution other than 1080p, which includes the launcher's default of 1440p. The whole frame is shown again.
- **Keys not found / game never started** on Linux when `XDG_DATA_HOME` was not set.
- **Windows build looked frozen** at the last step: the main game library has thousands of files and its link command overflowed Windows' command-line limit. It now uses a response file, and fails with a clear message if the linker cannot start.
- Launcher tabs now scroll instead of being cut off on small windows.
- The build uses up to 85% of your logical threads (hyper-threads included), starts the biggest pieces first, and links the game's libraries in parallel.

## Requirements and known limits

- A CPU with AVX2/BMI2/FMA (Intel Haswell / AMD Zen or newer).
- Linux AppImage: built on a very recent distro (glibc 2.44), so it starts only on new distros for now. A build on an older base is planned.
- Windows 11, 64-bit. Not signed. So far tested on one PC.
- Mods stay data-only. CTGP and texture upscaling are not part of this release.
