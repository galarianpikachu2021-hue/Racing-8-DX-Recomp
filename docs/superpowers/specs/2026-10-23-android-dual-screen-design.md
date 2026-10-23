# Android port + dual screen: design

Date: 2026-10-23. Status: APPROVED by the user (2026-10-23).
Related: the user's game version is 3.0.4 (some Eden reports use 3.0.5; the recompiler targets 3.0.4 only).

## Intent

Racing 8 DX Recomp runs today as a static (no-JIT) recompilation of the player's own MK8D 3.0.4 dump on Linux desktops. The next
step is a native Android version for dual-screen handhelds, the **AYN Thor** first (and similar "3DS-style" two-screen handhelds;
most other devices are too weak anyway), with a **second-screen companion** in the style of the "Mario Kart 8 Deluxe dual screen"
`.dsmod` the user supplied. The desktop gets the dual-screen feature too, as a second window.

Why: the user plays on a Thor. It should play better there than the Eden emulator does today, look like a polished phone/handheld
app ("phone people have higher standards"), and use the Thor's second screen the way that dsmod does.

## Goals

1. The game boots and races on the Thor through the static recompiler: **locked 60 fps at 720p** (handheld) is the requirement,
   **60 fps at 1080p** is the stretch goal. Tuned for Snapdragon 8-class SoCs and the Turnip (Mesa) Adreno Vulkan driver.
2. **Never slower than Eden** on the same Thor and settings. Benchmarked side by side, same course and scenario.
3. **Tight RAM**: must run on the base (lowest-RAM) Thor model. Budget RAM explicitly (guest memory, generated code, renderer
   caches, the build step), measure peak RSS on device.
4. A **new, clean Android launcher** (see Launcher).
5. **On-device build** of the generated code, within the user's 30-minute limit, with a progress bar and an opt-in background mode.
6. **Dual screen**: the bottom screen shows the companion pages from a user-supplied `.dsmod`, installed **as a mod**; desktop gets the
   same pages in a second window.

## Non-goals (now)

- Button remapping UI: the user is designing a binding menu for all platforms later. Leave room, build nothing.
- CTGP DX (paused), online play, texture upscaling (dropped), Windows.
- Distributing anything game-derived: the APK, like the repo, ships no game files, keys, firmware, recompiled code, hot-path data beyond
  the address list, or the `.dsmod` (someone else's work, no license; the user adds it).

## Anti-goals

- Running worse than Eden on the Thor.
- A cluttered launcher: no desktop-style settings tabs on Android.
- Making players supply or manage extra files and places: the dual-screen layout goes in through the normal mod flow, not a new folder.

## Launcher (Android)

Pages side by side, moved through by **swiping or the stick left/right**, tabs on top and page dots at the bottom (mockup:
`.superpowers/brainstorm/.../android-launcher.html`, approved: "the design is fire").

- **Home**
  - Not installed: one large drop area "Drop your game files here" (base game, update 3.0.4, keys, firmware) and "Select a folder"
    (Android document picker, folder access kept). Starts the build.
  - Building: progress bar (stages + percent). Opt-in "keep building when the app is closed" (Android foreground service with a
    progress notification).
  - Installed: big **PLAY**, and a separate **+ Add DLC** button placed far enough away to avoid accidental taps. Once DLC is added,
    the DLC button disappears and PLAY moves to the centre.
- **Mods**: on/off list (same on/off list the game's in-game Mods page uses) and "+ Add a mod". `.dsmod` zips are recognised here.
- **Controllers**: which controller each player uses, and a switch to **stop the Thor's built-in controls being Player 1** (for
  docked play on a TV). No button remapping (non-goal).
- **Miis**: as on desktop, cleaner.
- Everything else (renderer, upscaler, HDR, frame generation, speed limit, ...) lives in the **in-game menu**, kept as far as useful
  on Android, plus Android-specific upscalers (below).

- **Updates (Obtainium, user request)**: every GitHub release carries one signed, consistently named APK
  (`RacingDXRecomp-<version>-arm64-v8a.apk`, same signing key forever) so Obtainium can track and install it; the launcher offers
  "Get updates with Obtainium" (an `obtainium://add/<repo url>` link, with a plain fallback link when Obtainium isn't installed) and
  keeps its own GitHub-release check (as on desktop) for players without Obtainium. Updating the app never touches the built game; when
  a release changes the engine, the launcher offers to rebuild from the files the player already gave.

## In-game (Android)

- The existing overlay (F10 menu equivalent): opened by a controller combo (L3+R3 as today) and by touch; the Labo-style Mods page
  stays.
- Upscalers: FSR 1 (exists), **Snapdragon Game Super Resolution** (Qualcomm, open source, Vulkan-capable) as an Android option;
  FSR 3 only if it holds 60 fps on Adreno.

## Dual screen

- The `.dsmod` (format seen in `0100152000022000-MarioKart8DeluxeDS-1.0.0.dsmod.zip`): `manifest.json` = canvas 1240x1080 (the
  Thor's bottom screen), background, font from romfs, ~534 derived expressions, actions (use item, horn, theme/format toggles),
  page binds, haptics, 8 pages (pre-race / loading / race horn / race map, each light and dark) built from `rect`, `image`, `label`,
  `value`, `button` widgets with binds, repeats and animations (overtake card-swap); per-build JSON constants keyed by the game's build
  id; images and font are referenced from the game's own romfs (`romfs:/UI/USen/font.sarc#...`, `module:mk8d:lyt/...` layout
  textures).
- We implement **our own runtime** for that format:
  - a renderer for the widgets in our Vulkan backend (second swapchain: a second Android display / a second desktop window);
  - image and font loading from the player's own romfs (BNTX textures, BFFNT font);
  - the expression evaluator, page binds, touch actions and haptics;
  - the **values the format expects** (`mk.*`, `me.*`, `r0..r11.*`: phase, course/cup, ranks, items, map positions, horn state, ...)
    published by **our own code** reading the 3.0.4 game state. The mod's native `.so` modules are NOT loaded (closed binary,
    built for other game versions: 3.0.3, 4.0.0).
  - actions write back through our own hooks (use item, horn).
- Installed as a mod: the launcher's Mods page (both platforms) accepts the `.dsmod` zip into the normal mods folder; when enabled
  and a second screen exists, the companion shows; otherwise it does nothing.
- Order inside this part: build and iterate on the desktop (second window), then move to the Thor's second display.

## Engine on Android (how, decided in conversation as the builder's call, user OK'd "do all that")

- Same suyu fork (static, strict, no JIT) and native Vulkan renderer, built with the Android NDK for arm64-v8a.
- Generated code compiled **on the device** with a **bundled clang/lld**, using the tiered build (hot blocks `-O2`, cold `-O0`,
  `data/mk8d-3.0.4/hot_blocks.txt`), 5000-block units, job count from free RAM. Estimate 8-15 min on a Snapdragon 8 Gen 2 (to be
  measured).
- arm64 host for arm64 guest: look for cheaper memory access and instruction mappings than on x86 (to be measured, not promised).
- Turnip-first tuning; also run on Qualcomm's stock driver.

## Baseline to beat (Eden on the Thor, from EmuReady)

Community reports (emuready.com, Eden on AYN Thor, 6 approved listings, Oct 2025 - Sep 2026, all on Snapdragon 8 Gen 2 Thor Pro / Max,
none for the base model):

- All six rated "Perfect"; notes: "Perfect single-player"; "works perfectly 60fps with AA and FXAA on"; "Console in Performance mode,
  55-60 FPS ... only tested on first track", with Accuracy "Performance", Vertex Input Dynamic State on, DMA Accuracy "Unsafe";
  several mention a newer update than ours (3.0.5).
- So the bar is: **60 fps (55-60 in heavy moments) single-player at the default handheld resolution, in the Thor's Performance mode,
  with Eden's speed-over-accuracy settings**. Our target matches it at 720p with no accuracy trade-offs, and adds 1080p as the stretch.
- No base-model (lowest RAM) reports: RAM headroom there is unknown and is ours to measure.

## Testing (until direct device testing)

- The user is not connecting the Thor yet. Functional work (app, launcher pages, build service, dual-screen pages, input) is tested in the
  **Android emulator installed on the desktop** (arm64 or x86-64 image; generated code and engine built for the emulator's ABI).
- Performance numbers on device come later: the predefined test set is (a) first-track single-player race (Mario Kart Stadium, 150cc),
  (b) the effects scenario (drifts/items), (c) 2-player split-screen, each at 720p and 1080p, Thor Performance mode, compared with the
  EmuReady numbers above and, when the user can, a direct Eden run on the same device. Peak RSS is recorded in every run.

## Order of work

1. Engine boots and races on the Thor (hardest part); benchmark against Eden; RAM budget.
2. Android launcher + on-device build (progress, background mode).
3. Dual screen: desktop second window first, then the Thor.

## Constraints

- Legal: LEGAL.md applies unchanged (player supplies game, keys, firmware, `.dsmod`).
- Development machine: Linux desktop (AMD); an Android emulator is installed for app/UI work; real performance only on the Thor.
- Builds/tests on the shared desktop stay RAM-capped (see project notes).
- Push to GitHub only when the user asks, after an audit.

## Left to the builder

App architecture details, the build service design, how the engine is packaged in the APK (shared library loaded by the app),
storage layout on Android (app-specific storage + SAF folder grants), exact page visuals beyond the approved mockup, the
dual-screen runtime's internal structure, and which RE methods find the race-state values.

## Builder's calls (made while writing, flagged for review)

- Launcher in Kotlin + Jetpack Compose (not Qt).
- Snapdragon GSR as the Android upscaler.
- Dual-screen data from our own hooks, not the mod's `.so`.
- Desktop-first development of the dual-screen runtime.
- "+ Add DLC" hides once any DLC is added; further DLC can be added from the Mods page's "+ Add" (DLC files are recognised).
- Android emulator for functional testing until the Thor is connected; performance targets from the EmuReady data.
