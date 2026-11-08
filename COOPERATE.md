# Windows session: build the v0.2.2 Windows zip on this PC

You are the Claude Code session on the maintainer's **Windows PC**. The Linux session publishes the releases and cannot compile the Windows programs (they need Visual Studio and Qt for Windows). Your one job now: **build the standard v0.2.2 Windows zip locally, from the `main` source, and put it on the USB drive.** v0.2.2 fixes GitHub issues #2/#8 (firmware newer than 22.5 refused with a clear message; mod `exefs` code ignored), #7 (Online Play no longer freezes) and #9 (exporter answer parsing). The Linux session will then replace the release file with yours. The maintainer's pronouns are unknown: use "they". Be concise and report faithfully, including anything that failed or was not tested.

Why locally: GitHub Actions minutes may be billed. **Do not start, re-run or trigger any GitHub Actions run.** (The Windows workflows are now manual-only.)

## Hard rules

- **No GitHub changes at all:** no push, no tag, no release or asset edits, no PR, no branch changes. The Linux session owns GitHub. This is a read-only checkout of `main`.
- **A standard release, nothing extra:** no test flags, no debug switches, no engine-version pin, no local patches, no source edits. If the source does not build as it is, stop and describe the problem in `BUILD-NOTES.txt` (see the end); do not fix it in the zip.
- **NO JIT, ever.** Nothing from the game in the zip: no `.nsp`, `.nca`, keys, firmware, `recompiled_*`, no saves or `userdata`. **Nothing from NVIDIA** (no `nvngx*`, no `*dlss*`, no NGX): DLSS is never redistributed.
- Do not touch the maintainer's own files on the USB drive (NSPs, firmware, keys). Quote paths with spaces. One game instance at a time; no process kills by pattern.

## 1. Get the exact source

`git fetch origin` and check out `origin/main` (detached is fine, in a **separate folder** from any working tree with local changes). Expected: `CMakeLists.txt` says `project(KartRecomp VERSION 0.2.2 ...)`; `main` is at the commit that made that change or later with only docs and `COOPERATE.md` changed since. Patch `0002` and `app/` changed for v0.2.2 (that is expected).

Patches: `patches/suyu/0001`, `0002`, `0003` only (there is no `0004` any more; mimalloc is inside `0001`). `packaging/windows/fetch-fork.sh <dir>` applies them to the pinned upstream. **Windows line endings:** an empty context line in a patch breaks under CRLF, and the engine-version hash changes with line endings, so use `git -c core.autocrlf=false` for this checkout.

## 2. Build locally (what you already did on this PC before)

Same steps as `.github/workflows/windows-engine.yml` (read it for the exact commands), with your own Visual Studio 2022 Build Tools, CMake, Ninja, the Vulkan SDK and Qt 6.7:

1. **Engine and exporter:** `fetch-fork.sh fork`, then configure with `-DCMAKE_BUILD_TYPE=Release -DENABLE_QT=OFF -DYUZU_TESTS=OFF -DENABLE_WEB_SERVICE=OFF -DSUYU_NO_JIT=ON -DKARTRECOMP_ENGINE=ON -DKARTRECOMP_ENGINE_VERSION=9586f9ea01-abi6` and build `mk8d-engine` and `kartrecomp-export`. **Use exactly `9586f9ea01-abi6` as the engine version, do not recompute it.** It is the stamp inside the published v0.2.1 zip, and v0.2.2 changes no generated code or module ABI (only HLE services and the launcher), so games players already built keep working. Write the same text to `engine\ENGINE_VERSION`.
2. **Launcher:** build `kartrecomp.exe`, `kartrecomp-setup.exe`, `kartrecomp-build.exe` from `app/` with Qt 6.7 + zlib (the `launcher` job in the workflow), then `windeployqt --release --no-translations`. They must report version **0.2.2**.
3. **Toolchain and plugins:** reuse them from the published zip, they are unchanged and were built by CI: copy `toolchain\`, `engine\plugins\` (FSR 3, FSR 4, the three AMD DLLs and `AMD-FSR-SDK-LICENSE.txt`) and `data\` out of the published v0.2.1 zip (download `KartRecomp-windows-x64.zip` from the v0.2.1 release page; reading it is fine, do not change the release). (If you prefer to rebuild the plugins yourself with `plugins/fsr3|fsr4/build-windows.ps1`, say so in the notes; the FSR 3 shader blobs come from the Linux-built `ffx-prepared` data, so reusing is simpler.)
4. **mimalloc licence:** `engine\mimalloc-LICENSE.txt` (from the fetched mimalloc source under your build's `_deps`, or from the published v0.2.1 zip).

## 3. Assemble and check

- `packaging/windows/assemble.ps1 -Engine <engine stage> -Launcher <launcher stage> -Toolchain <toolchain> -Plugins <plugins> -Out KartRecomp` (read the script for the stage layouts; the engine stage holds `mk8d-engine.exe`, `kartrecomp-export.exe`, `ENGINE_VERSION`, `mimalloc-LICENSE.txt`).
- `tests/windows/test_release_contents.ps1 -Dir KartRecomp -RequirePlugins` must pass. Also confirm by hand: `KartRecomp\tools\kartrecomp-export.exe` exists next to `kartrecomp.exe`'s `tools` folder; `README-WINDOWS.txt` does not say TEST BUILD; no `engine-version.pin` effect (the version is the explicit one above); no game files, no NVIDIA files.
- Run the new `kartrecomp.exe` once: the Setup tab must find the exporter (rows say what is missing about the files, not "The exporter is missing"), and the update check must not offer an update (it may say 0.2.2 is newer than the latest release until that release is published; that is fine).
- Zip it as `KartRecomp-windows-x64.zip` with the single top folder `KartRecomp\`, and write `KartRecomp-windows-x64.zip.sha256` containing exactly `<lowercase hash>  KartRecomp-windows-x64.zip` (two spaces).

## 4. Hand it over on the USB drive

Drive label **racing8test** (it was reformatted to exFAT on 2026-11-07 and is empty apart from what you put there; the old v0.2.1 folders are gone). Create a new folder `release-v0.2.2-local\` and put in it: the zip, its `.sha256`, and `BUILD-NOTES.txt` with:
- the `main` commit you built, your Visual Studio, Qt and CMake versions;
- the SHA-256 of `kartrecomp.exe`, `engine\mk8d-engine.exe`, `tools\kartrecomp-export.exe`;
- the list of files in your zip that differ from the published one (file names and sizes only);
- anything that failed, was skipped, or you did not test.

When finished, tell the maintainer in one short message where the folder is. The maintainer then asks the Linux session to compare it with the published zip and replace the release file.

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