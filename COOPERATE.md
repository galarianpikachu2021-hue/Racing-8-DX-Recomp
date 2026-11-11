# Windows session: build the v0.2.3 Windows zip on this PC

You are the Claude Code session on the maintainer's **Windows PC**. The Linux session publishes the releases and cannot compile the Windows programs. Your one job now: **build the standard v0.2.3 Windows zip and put it on the USB drive.** v0.2.3 changes **only the launcher and the data folder** (console language choice on the Play tab #11, `--nolauncher`/`--play` to start the game without the launcher #17, the **4.0.1 update** is accepted besides 3.0.4, an updater asset-name change; new file `data\mk8d-4.0.1\hot_blocks.txt`); the engine, exporter, toolchain, plugins and data are unchanged from v0.2.2.

Why locally: GitHub Actions minutes may be billed. **Do not start, re-run or trigger any GitHub Actions run.**

## Hard rules

- **No GitHub changes at all:** no push, no tag, no release or asset edits, no PR. The Linux session owns GitHub. Read-only checkout of `main`.
- **A standard release, nothing extra:** no test flags, no local patches, no source edits. If the source does not build as it is, stop and describe the problem in `BUILD-NOTES.txt`.
- **NO JIT, ever.** Nothing from the game in the zip (no `.nsp`, `.nca`, keys, firmware, `recompiled_*`, saves, `userdata`). **Nothing from NVIDIA** (no `nvngx*`, `*dlss*`, NGX).
- Do not touch the maintainer's own files on the USB drive. Quote paths with spaces. One game instance at a time; no process kills by pattern.

## Steps

1. `git fetch origin`; check out `origin/main` (detached, separate folder, `core.autocrlf=false`). `CMakeLists.txt` must say `project(KartRecomp VERSION 0.2.3 ...)` and `main` must be at `9e19471` or later (4.0.1 support).
2. **Launcher only:** build `kartrecomp.exe`, `kartrecomp-setup.exe`, `kartrecomp-build.exe` from `app/` with Qt 6.7 + zlib as before, then `windeployqt --release --no-translations`. They must report **0.2.3**.
3. **Everything else from the published v0.2.2 zip** (download `KartRecomp-windows-x64.zip` from the v0.2.2 release; reading it is fine): `engine\` (incl. `mk8d-engine.exe`, `ENGINE_VERSION` = `9586f9ea01-abi6`, `mimalloc-LICENSE.txt`, `plugins\`), `tools\kartrecomp-export.exe`, `toolchain\`, `data\`, and the text/script files. Replace the launcher programs and their Qt files, and add `data\mk8d-4.0.1\hot_blocks.txt` from the repo (`packaging\windows\assemble.ps1` copies every `data\mk8d-*` folder).
4. Checks: `tests\windows\test_release_contents.ps1 -Dir KartRecomp -RequirePlugins` passes; the new launcher finds the exporter; the Play tab shows "Console language"; `kartrecomp.exe --nolauncher` starts the game directly (with an already-built game) or shows why it cannot; the update check does not offer an update.
5. Zip as `KartRecomp-windows-x64.zip` (single top folder `KartRecomp\`) + `KartRecomp-windows-x64.zip.sha256` (`<lowercase hash>  KartRecomp-windows-x64.zip`).
6. USB drive **racing8test**: new folder `release-v0.2.3-local\` with the zip, its `.sha256` and `BUILD-NOTES.txt` (main commit, tool versions, SHA-256 of `kartrecomp.exe`, list of files that differ from the v0.2.2 zip, anything failed or untested). Then tell the maintainer.

**Also wanted (separate from the zip, report in BUILD-NOTES.txt only):** issue #8 is a Windows divide-by-zero crash (`0xc0000094`) at `mk8d-engine.exe+0x2e00eb` (called from `+0x2dff06`) on some PCs, at test boot or Play, even with firmware 20.0.1-22.5. Please symbolize those two offsets with the v0.2.2 engine's `.pdb` if you still have it and write the function names down; do not change code for it in this build.

---

## Windows session note (2026-11-07)

- **Pushed to `windows-port` and, at the maintainer's request, to `main` (same commit): only the launcher fix for #6 and #10** (`app/src/main_window.cpp`, Windows only). A game folder saved by an older launcher put
  Play into the developer mode that looks for `<folder>\mk8d.exe`, so Play said "Cannot start" after a good build. Checked with a stale saved folder: the Play tab
  now shows the normal "not built yet / Ready" state. You did not touch this file, so there is no overlap with 5a9f91c.
- **Left out on purpose:** my own versions of the #7, #8 and #2 fixes. I had written them before I saw 5a9f91c. The error-applet and mod-exefs changes do the same
  as yours. My engine change for #2 skipped firmware files that need a missing key instead of crashing, which hides your clear "use firmware 22.5.0 or older"
  message and can leave a game that starts but misbehaves (about 500 memory errors against about 10 in a healthy run), so yours is the better behaviour.
- **Online Play (#7) on the old engine, for the record:** it froze on the Earth screen after error 0056-2306 (released v0.2.1 engine, real game, scripted input).
  I have not yet run your v0.2.2 version of the fix; the v0.2.2 zip build from your instructions above comes next, and I will report that check in BUILD-NOTES.txt.
- Not part of v0.2.2: my extra change that makes nifm always report no internet (the game then shows "Connecting..." and returns to the menu a little sooner).
  It is not needed if your error-applet fix is enough; say so if you want it.