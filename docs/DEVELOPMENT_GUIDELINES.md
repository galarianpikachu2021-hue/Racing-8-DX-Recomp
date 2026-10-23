# Development Guidelines: KartRecomp Build & Testing Pipeline

*This document contains deep technical information regarding the build process, hardware targets, and testing procedures for kart-recomp. Use this as a reference guide; do not embed these details directly into daily operational instructions unless necessary.*

## ⚠️ Critical Hard Rules (Adherence is Mandatory)
1.  **Data Isolation:** Never commit or publish game data, keys, firmware, or anything generated from the game (generated C, objects, libraries, the built game binary, saves). All such artifacts must live in `local/` and are gitignored.
2.  **Legal & Licensing:** Always read `LEGAL.md` before adding any new files. Remember that the upstream mk8-recomp has no license; reference it, but do not copy its files directly.
3.  **GitHub Protocol:** Do not create remotes, push, or open PRs unless explicitly directed by the user. The user must publish manually.

## 🛠 Build System & Structure Overview
*   **Working Static Build:** `local/game/` is the primary static build location. Running it requires executing: `local/game/play.sh`.
*   **Key Management:** Keys and firmware are stored in `local/suyu-home/` (which must not have an update installed).
*   **Source Workspace:** The original research workspace is `<research workspace>/mk8-recomp`. Its build trees are `build/suyu-nojit` (static target) and `build/suyu`. Running notes reside in `local/NOTES.md`.
*   **Relinking after Suyu Edit:** The standard procedure for relinking patches is:
    1.  Run the ninja command: `ninja -C .../build/suyu-nojit -j2 suyu-cmd-static`
    2.  Copy the static binary: Copy `bin/suyu-cmd-static` to `local/game/mk8d`.

## 🔋 Resource Management & Performance
*   **RAM Constraints:** The total available RAM is approximately 15 GB, considering other user processes. For heavy jobs, use:
    `systemd-run --user --scope -p MemoryMax=<MB_Limit>`
*   **Parallel Compilation:** Always use `-j1` when compiling generated C code to mitigate memory spikes.

## 🧪 Testing & Optimization Procedures

### 1. Coverage Checking (Sanity Check)
Before any major test run, verify compilation completeness:
*   Check `local/game/user/log/recomp_coverage.txt`. It must report `fully static`.
*   The main session log is located at `user/log/suyu_log.txt`.

### 2. Native GPU & Tracer Work (Advanced)
*   **Reference:** Consult `docs/NATIVE_GPU.md` for details.
*   **Hook Seam:** The core hook location is `core/arm/recomp/host_hooks.{h,cpp}` within the suyu fork (specifically in `dispatcher`, before block lookup).
*   **Tracer Setup:** Use the tracer files: `nvn_trace.{h,cpp}`, enabled via environment variable: `KARTRECOMP_NVN_TRACE=1`. Output goes to `user/log/nvn_trace.txt`.
*   **Synchronization Patches (Keeping Sync):** To keep `patches/suyu/` synchronized with the fork, run differential comparisons:
    *   For **0001** (Explicit Content, DLC, HLE applets, etc.):
        `git diff -- src/suyu_cmd plus src/input_common`
    *   For **0002** (Host hooks, NVN tracer, RE logs):
        `git diff -- src/core src/common src/video_core`
*   New files must be added to git first: `git add -N`.

### 3. Rendering Modes
*   Set the rendering environment variable based on testing needs:
    *   **Native Only:** `KARTRECOMP_NATIVE_GPU=native` (suyu runs as Null)
    *   **Comparison:** `KARTRECOMP_NATIVE_GPU=shadow` (runs both for comparison)
*   **Unattended Visual Checks:** Use a script and specific environment variables to capture frames:
    `tools/make-menu-tas.py local/game/user/tas/script0-1.txt` then run `play.sh -t` with `KARTRECOMP_NATIVE_SHOTS=N`. (Frames land in `user/log/native_dump/shots/`).

### 4. CPU Profiling and Benchmarking
*   **Profiling Setup:** Set the profile environment variable:
    `export KARTRECOMP_PROFILE=<start_s>,<seconds>[,wall]` (+ `KARTRECOMP_PROFILE_OUT`)
*   **Execution:** Run the test suite:
    `./tools/profile-report.py <file> local/game/mk8d`
*   **Benchmark Tip:** To benchmark stability, use `make-menu-tas.py --centre-stick`.

## 🚀 Advanced Development Flow: Block Chaining Implementation
*   **(Status Update):** The dispatcher loop now directly reads block indices (neutral change).
*   **(Goal/Plan):** Implement 'block chaining' using `tools/recomp-chain.py <exefs dir>`. This rewrites the generated C to use a direct B/BL fall-through (`musttail`) instead of returning to the dispatcher, while maintaining checks for halted/SVC/budget limits.
*   **(Mechanism):** The process involves editing the research workspace's generated C in place (in `~/reverse-engineering/mk8d/mk8-recomp/local/exefs-304b/*/src`) and adding a `RECOMP_TAIL` macro to each module's `recomp_runtime.h`.
*   **(Dependencies):** The generator's `ChainTo` function in the fork's arm64\_to\_c.h must be patched to maintain matching text for new exports.
*   **(Testing Steps):** 1) Test-compile one unit and check for `musttail` errors. 2) Rebuild only hot units and run them. 3) Full rebuild (Estimated: ~2.5 hours at `-j1`, MemoryMax-capped).
*   **(Baseline Metrics):** Racing baseline is typically 58–60 fps, dipping to 52–55 fps. Core utilization shows specific overheads in memory helpers and dispatch loops (e.g., 26.8% / 17.6%).