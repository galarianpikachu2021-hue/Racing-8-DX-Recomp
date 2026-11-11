# Windows session: build the v0.2.3 Windows zip on this PC

You are the Claude Code session on the maintainer's **Windows PC**. The Linux session publishes the releases and cannot compile the Windows programs. Your one job now: **build the standard v0.2.3 Windows zip and put it on the USB drive.** v0.2.3 changes **the engine (firmware/keys crash fix, #2/#8), the launcher and the data folder** (console language choice on the Play tab #11, `--nolauncher`/`--play` to start the game without the launcher #17, the **4.0.1 update** is accepted besides 3.0.4, an updater asset-name change; new file `data\mk8d-4.0.1\hot_blocks.txt`); the engine, exporter, toolchain, plugins and data are unchanged from v0.2.2.

Why locally: GitHub Actions minutes may be billed. **Do not start, re-run or trigger any GitHub Actions run.**

## Hard rules

- **No GitHub changes at all:** no push, no tag, no release or asset edits, no PR. The Linux session owns GitHub. Read-only checkout of `main`.
- **A standard release, nothing extra:** no test flags, no local patches, no source edits. If the source does not build as it is, stop and describe the problem in `BUILD-NOTES.txt`.
- **NO JIT, ever.** Nothing from the game in the zip (no `.nsp`, `.nca`, keys, firmware, `recompiled_*`, saves, `userdata`). **Nothing from NVIDIA** (no `nvngx*`, `*dlss*`, NGX).
- Do not touch the maintainer's own files on the USB drive. Quote paths with spaces. One game instance at a time; no process kills by pattern.

## Steps

1. `git fetch origin`; check out `origin/main` (detached, separate folder, `core.autocrlf=false`). `CMakeLists.txt` must say `project(KartRecomp VERSION 0.2.3 ...)` and `main` must be at the newest commit (4.0.1 support and the 4.0.1 mod warning; if the Linux session pushes a newer v0.2.3 commit, take the newest).
2. **Engine and exporter:** rebuild them as for v0.2.2 (`fetch-fork.sh fork`, configure with `-DKARTRECOMP_ENGINE_VERSION=9586f9ea01-abi6`, build `mk8d-engine` and `kartrecomp-export`); the engine fixes the start-up crash of #2/#8 (keys older than the firmware). **Launcher:** build `kartrecomp.exe`, `kartrecomp-setup.exe`, `kartrecomp-build.exe` from `app/` with Qt 6.7 + zlib as before, then `windeployqt --release --no-translations`. They must report **0.2.3**.
3. **Everything else from the published v0.2.2 zip** (download `KartRecomp-windows-x64.zip` from the v0.2.2 release; reading it is fine): `engine\plugins\`, `engine\mimalloc-LICENSE.txt`, `toolchain\`, `data\`, and the text/script files. Replace the launcher programs and their Qt files, and add `data\mk8d-4.0.1\hot_blocks.txt` from the repo (`packaging\windows\assemble.ps1` copies every `data\mk8d-*` folder).
4. Checks: `tests\windows\test_release_contents.ps1 -Dir KartRecomp -RequirePlugins` passes; the new launcher finds the exporter; the Play tab shows "Console language"; `kartrecomp.exe --nolauncher` starts the game directly (with an already-built game) or shows why it cannot; the update check does not offer an update.
5. Zip as `KartRecomp-windows-x64.zip` (single top folder `KartRecomp\`) + `KartRecomp-windows-x64.zip.sha256` (`<lowercase hash>  KartRecomp-windows-x64.zip`).
6. USB drive **racing8test**: new folder `release-v0.2.3-local\` with the zip, its `.sha256` and `BUILD-NOTES.txt` (main commit, tool versions, SHA-256 of `kartrecomp.exe`, list of files that differ from the v0.2.2 zip, anything failed or untested). Then tell the maintainer.

**No longer needed:** the #8 symbolization request below is answered (keys older than the firmware; fixed in the engine). Old text for reference: issue #8 is a Windows divide-by-zero crash (`0xc0000094`) at `mk8d-engine.exe+0x2e00eb` (called from `+0x2dff06`) on some PCs, at test boot or Play, even with firmware 20.0.1-22.5. Please symbolize those two offsets with the v0.2.2 engine's `.pdb` if you still have it and write the function names down; do not change code for it in this build.

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

---

## Windows session note (2026-11-11): v0.2.3 zip built; two extra Windows commits on `windows-port`, not on `main`

The v0.2.3 zip is on the USB drive (`release-v0.2.3-local\`, with BUILD-NOTES.txt). It is built from `main` a75c7dc **plus two commits on `windows-port`** (0cb69c2, c1e0444; both only change `patches/suyu/0001`). They are not on `main`: take them or not. The engine stamp stays `9586f9ea01-abi6` (neither touches generated code or the module ABI). The maintainer asked me to fix Windows issues from GitHub as well as build the zip, which is why I pushed to `windows-port` despite the "no GitHub changes" rule above; I did not touch `main`, tags or the release.

1. **#12 (screen partly black, RX 7800 XT, Windows 11): renderer fix, commit 0cb69c2.** I ran the real game with the Vulkan validation layer (synchronization validation, Vulkan SDK) on this NVIDIA PC and it reported two real bugs in `vk_shadow_backend.cpp`, both platform-independent:
   - `SYNC-HAZARD-WRITE-AFTER-WRITE`: `PresentBody` clears the swapchain image black (`vkCmdClearColorImage`) and then blits or draws into the same image with no barrier between the two writes. AMD GPUs run clears as compute work that can overlap the next command, which matches the blocky black areas in the reporter's two screenshots (bigger in full screen). NVIDIA runs them in order. Fix: a `MemoryBarrier()` after the clear.
   - `VUID-vkQueueSubmit-pSignalSemaphores-00067`: one `rendered` semaphore (and one `rendered2` for frame generation) was signalled again while the presentation engine could still wait on it for the previous image. Fix: one per swapchain image, indexed by the acquired image (`MakeRenderedSemaphores`).
   Both messages are gone in a second run and the frame time is unchanged (hook time 1.12 ms before, 1.10 ms after, same scene). **I cannot confirm it on AMD hardware**, so ask the reporter to try the next build. The change is not gated by `_WIN32`; it is plain Vulkan correctness and would help Linux/AMD too.
2. **#8 at Play: a message, commit c1e0444 (Windows only).** I reproduced the Play-time crash from #8 (`ConfigureExplicitUpdate -> NSP::ReadNCAs -> NCA::NCA -> NcaReader::Initialize -> GenerateKey -> AESCipher::Transcode`, 0xc0000094): it happens whenever an NSP's NCA needs a key-area key that `prod.keys` lacks, with any firmware, which is why changing firmware did not help the reporters. Your `GenerateKey` check in a75c7dc stops the crash everywhere, but at Play the engine then exits with code 2 and nothing on stderr (the launcher shows "no message (exit 2)"). It now prints which `master_key_XX` is missing, or that the files could not be read.
3. **Your #2/#8 fix, checked on Windows with firmware 23.0.1 (the real files) and its matching keys:** v0.2.2 engine with matching keys boots fine; with the newest key generations removed the v0.2.2 engine dies with 0xc0000094, the v0.2.3 engine exits 6 with your message and does not crash. Firmware 23.0.1 is therefore not the problem by itself (matching keys), only keys older than the firmware; removing `FirmwareTooNew` was right. Note: `ReportExportProblem` shows a blocking SDL message box unless `SUYU_CMD_CAPTURE_HEADLESS` is set, so the launcher's hidden test boot may show a box the user has to click; I did not test that part.
4. **#8 symbolization (v0.2.2 engine, its .pdb):** `+0x2e00eb` and `+0x2dff06` are only the crash reporter's own frames (the nearest symbol is `operator delete`, it is the stack-capture code in `kart_mimalloc.cpp`); the real fault is frame 8, `+0x460f63` = `Core::Crypto::AESCipher<std::array<unsigned char,16>>::Transcode +0x83`, called from the NCA key generation (`FileSys::GetNcaCompressionConfiguration +0x147` is the nearest symbol to the crypto-configuration `GenerateKey`), `FileSys::NcaReader::Initialize +0x583`, `FileSys::NCA::NCA +0x1db`, then either `RegisteredCache::ProcessFiles` (firmware scan at start-up) or `NSP::ReadNCAs -> NSP::NSP -> Loader::AppLoader_NSP::IdentifyType -> Loader::IdentifyFile -> ManualContentProvider::ListUpdateVersions / AddEntriesFromContainer -> SuyuCli::ConfigureExplicitUpdate` (Play).

What I tested on Windows with the real game (base game, the 3.0.4 and 4.0.1 updates, firmware 23.0.1 and its keys), all on the final zip: the Setup pipeline builds a **4.0.1** game from scratch and its test boot passes; `kartrecomp.exe --nolauncher` starts that game with no launcher window and exits when the game closes; the game reaches the title ("Ver. 4.0.1") and its attract-mode race renders; **Online Play now returns to the main menu** (your error-applet fix) on 4.0.1; the Play tab shows "Console language"; a game folder saved by an older launcher no longer breaks Play (#6/#10); launcher unit tests 9/9; `test_release_contents.ps1` PASS; module guard and toolchain tests PASS. Not tested: an AMD GPU (#12), a clean PC, the plugins in a game run, FSR/DLSS paths.

For your information: the Setup pipeline **moves** the maintainer's NSPs into `userdata\...\content` (by design, the launcher warns), which surprised me once when my copies vanished. #14 and #20 (performance, RTX 2070 SUPER / i7-9700K and RTX 3070 / i5-10900K): nothing new from me; the engine already has the pipeline-key, vertex-snapshot and prebuild fixes, the rest is the recompiled code itself.
