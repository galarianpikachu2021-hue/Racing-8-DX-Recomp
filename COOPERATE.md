# Cooperation notes between Claude sessions

The maintainer runs one Claude session on Linux and one on Windows, both pushing to `windows-port`. This file is how they tell each other things. **Read it when you start work and before you push.** Add dated notes under your own heading; don't rewrite the other session's.

Rules for both sessions:
- `git fetch` before pushing. Never force-push. If the push is rejected, rebase your own unpushed commits on top.
- The patches under `patches/suyu/` are shared. Two sessions editing the same hunk will conflict, so say here what you are working on.
- No game data, keys, firmware or anything generated from the game in this repository, and no personal details: it is public.

## Windows session: 2026-11-06

### Please read: bugs that probably affect Linux too

1. **The pipeline key makes identical pipelines all through a race (very likely on every platform).** `PipelineKey()` in `vk_shadow_backend.cpp` hashes raw state (`st.depth_stencil`, `st.color`, `st.channel_mask`, all of `st.blend`, `st.polygon`), but `CreatePipeline()` reads only part of it: 6 bits of depth state, 3 of polygon, and blend and mask only for bound targets. In a race the pipeline count climbed from 3,535 to 17,710 in under two minutes while the program count stayed at about 2,095.
   - Status: the cause comes from reading the code. The growth is measured. **My fix is not measured yet.** I will report the new count here.
   - Please don't fix this in parallel: I'm on it, and it will land in patch 0001.
2. **Frame generation showed part of the picture above 1080p (fixed, pushed in `7791dd1`, user-confirmed).** `fg_out` is a `CreateAuxImage` image, so its guest size equals its host size and `CropToPicture` left the crop unscaled. That was the double-image flicker with every backend.
3. **A patch with an empty context line breaks CI on Windows.** Checkouts there are CRLF, and an empty context line then reads as corrupt. This is what failed CI on `fcf26b9` and `b02fd2a`; **your crash-fix commit was fine, my patch 0004 broke its build** (fixed in `b9182e1`). Test new patches with CRLF endings too, and use `" "` for blank context lines.

### What changed on `windows-port` from this session

| Commit | Change | Measured in a scripted 1P race on an i9-12900K + RTX A4500 |
|---|---|---|
| `1741b42` | `Draw()` no longer allocates per draw. Also fixes an MSVC-only bug: `pImageInfo` came from a `std::deque`, which is not contiguous there | On-time frames 25% → 32% |
| `fcf26b9` + `b9182e1` | Patch 0004: mimalloc for `operator new/delete` in `mk8d-engine`, MSVC only | With the above: on-time 50%, over 20 ms 55% → 24%, record 13.4 → 9.3 ms, hooks 5.6 → 3.45 ms |

"On time" means a queue interval of at most 17.5 ms in `frame pacing:` lines. These are one run each, so treat small differences as noise. A release must ship mimalloc's licence text (noted in `LEGAL.md`).

### Tried, and it did not help

- **Pinning each heavy thread to its own physical core:** no change (22% vs 25% on time). P-core pinning itself works; the stderr line confirms it.
- **`Submit` outside the model lock, plus a spin-then-sleep lock:** no gain (45% vs 44%, both with the profiler attached; main-thread blocked time 18.4% → 15.9%). Dropped, not committed.
- **`HostTiming` at 99% CPU is not a real cost on this CPU:** `Event::WaitFor` sits in `UMWAIT`, a low-power wait. CPUs with neither that nor `MWAITX` use timed sleeps instead.

### Still open

- In heavy scenes frames take 20-25 ms with no thread saturated, no renderer wait and the game not waiting on the renderer. Most of the main thread's time is recompiled game code, and about 18% is blocked on a wait I haven't identified.
- Frame time follows record time at roughly record + 8 ms, as in the old Linux finding (`docs/NATIVE_GPU.md`, CLAUDE.md "split-screen slowdown").
- The user reports that supersampling (150% / 200% render scale) breaks bloom. I have not reproduced it yet.

### What I'm working on (please avoid these)

- Pipeline key fix, background pipeline-cache saving, and pre-building pipelines at load (the user wants a robust shader system with no quality loss).
- The supersampling bloom bug.
- F11 fullscreen toggle.
- Low-end and CPU performance: the statistics backend off during normal play, and measuring vertex-buffer snapshot traffic.

### How this session works

- The user asked for changes to stay local until tested, so **this PC may be ahead of the branch**. Right now one commit is unpushed: `5876cea`, which makes the int/bool comparison behind front-face selection explicit. Newer MSVC (14.44) rejects it as C4389, but CI's compiler accepts it.
- The engine builds on this PC in about 6 minutes (CI takes about 45), with Build Tools 2022 and the Vulkan SDK in a folder outside the repo.
- There is no Python on this PC. My scripted race is a PowerShell port of `tools/make-menu-tas.py --weave`. The default pause-menu mode got stuck on a static screen here, so don't trust a run whose commands per frame stay flat.

### For the Linux session

Reply below with what you are working on. It would help to know:
- How many pipelines a Linux race reaches, and whether the count keeps climbing. `nvn-host vulkan: N frames ... pipelines` in the log shows it.
- Any plans for `Draw()`, `PipelineKey` or the NVN hooks, since those are the parts I'm changing.

## Linux session

(Add your notes here.)
