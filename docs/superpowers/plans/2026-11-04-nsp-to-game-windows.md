# NSP-to-game builder, Windows: plan stub

Status: not started. Follows `2026-11-04-nsp-to-game-linux.md` (spec: `docs/superpowers/specs/2026-11-04-nsp-to-game-builder-design.md`).
Decisions already made (user): Windows and Linux both; FSR 3.x and DLSS plugins in the first Windows build, built against the CURRENT Windows
SDKs (not ports of the Linux plugins); strict no JIT; speed no slower than today; a functional Windows build goes to the USB drive "racing 8 testing".

What the Linux work already gives Windows: the fork loads `recompiled_*.dll` modules (`LoadModuleDir`, the Windows path kept in
`frontend_common/kart_static_recomp.cpp`); the exporter is Qt-free; the launcher builds on Windows (`.github/workflows/windows-launcher.yml`);
the pipeline (`setup_pipeline.cpp`) is platform-neutral except `/proc/meminfo` and the symlink step.

Work to plan (a real plan comes first, written with the writing-plans skill):
1. Engine for Windows: native Vulkan renderer window (SDL3), guest memory (fastmem/page tables), input, timing, plugin loading (`.dll`),
   logging/XDG-equivalents (`%APPDATA%`), thread pinning. Strict no-JIT build (`SUYU_NO_JIT=ON`).
2. Toolchain for Windows: llvm-mingw (clang + lld + mingw-w64 runtime; MSVC/Windows SDK cannot be redistributed); module `.dll` build flags;
   the same glibc-free sysroot idea does not apply.
3. Launcher: Windows paths (`%LOCALAPPDATA%`/`%APPDATA%`), RAM query (`GlobalMemoryStatusEx`), links (junction/hardlink or copy for NSPs),
   `Process` handling, no AppImage packaging; a zip/installer layout with `engine/`, `toolchain/`, `data/`.
4. Plugins: FSR 3.x upscaling and frame generation, DLSS (NGX) against the current Windows SDKs; check which backends exist for Vulkan and
   report any Windows-only newer upscaler before choosing.
5. Verification: Null-renderer boot test on Windows (SDL dummy driver equivalent), acceptance run on the user's Windows PC.
