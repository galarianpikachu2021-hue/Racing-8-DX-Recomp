# Racing 8 DX Recomp v0.2.0

You bring your own game files; nothing from the game is in this release.

## New: easy install, no command line

The launcher now builds the game for you. Open the **Setup** tab, pick your own files, press **Build game**, then **Play**.

- Pick: base game `.nsp`, the 3.0.4 update `.nsp`, optional DLC `.nsp`, your `prod.keys`, and the firmware (a folder or the `.zip`).
- Every row shows a tick or a plain-language reason (wrong update version, missing key, and so on).
- The build takes about 5 minutes on an 8-thread PC (about 3 GB of free RAM and 7 GB of disk). **Cancel** at any point; **Build** resumes where it stopped.
- Your `.nsp` files are **moved** into the game's folder after a warning, so the game does not need the original files. Keys and firmware are copied.
- A tick on every row after a build; the Setup page shows what is installed even if its settings were reset.
- No JIT anywhere: the game is statically recompiled on your PC from your own files.

### Upscalers and frame generation

- **FSR 3.1** upscaling and **FSR 3 frame generation** work on any Vulkan GPU, with a **Model** row under Upscaler in the in-game menu (F10) for plugins that offer several models.
- The render scale and the upscaler now apply only to the 3D scene in a race. Menus and their 3D previews (character and kart select) draw at the full output size.
- FSR 4 and DLSS plugins exist for Windows and are not part of this release (see below).

### Windows

This release is the Linux AppImage. A Windows build exists on the `windows-port` branch and is still being tuned (frame generation, performance); it will get its own release.

## Fixed

- **Crash when hovering a modded driver in character select** (a null call in the game's camera code): the thread now returns from the call instead of ending, so the game keeps running.
- **Fine diagonal hatching (shadow acne)** on karts and characters, worst in the character select preview: shadow maps use a stronger depth offset.
- **Only a part of the picture was visible** (zoomed into the top-left) at any output resolution other than 1080p, which includes the launcher's default of 1440p. The whole frame is shown again.
- **Keys not found / game never started** on Linux when `XDG_DATA_HOME` was not set.
- Launcher tabs now scroll instead of being cut off on small windows.
- The build uses up to 85% of your logical threads (hyper-threads included), starts the biggest pieces first, and links the game's libraries in parallel.

## Requirements and known limits

- A CPU with AVX2/BMI2/FMA (Intel Haswell / AMD Zen or newer).
- Linux AppImage: built on a very recent distro (glibc 2.44), so it starts only on new distros for now. A build on an older base is planned.
- Mods stay data-only. CTGP and texture upscaling are not part of this release.
