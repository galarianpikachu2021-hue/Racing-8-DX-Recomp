# Native performance project (branch `native-perf`, local)

Goal (user, 2026-11-11): make the CPU side fully native, strip the emulated console OS layer down to what the game needs
(LASRecomp-style static program), keep the testing loop fast, and run well on Intel integrated GPUs (laptops, Windows and Linux).
Strict rule unchanged: no JIT anywhere.

## Measuring

- Scenario: `tools/make-menu-tas.py --effects` race, `KARTRECOMP_NO_MODS=1`, native renderer without scaling, profile from 60 s for
  60 s (CPU mode). `local/exp/ab.sh <label> <module dir>` runs one race with the release engine (`local/game/mk8d-engine`, v0.2.3,
  stamp `d201f1ec0e-abi6`) and a module folder, restores the configs, and prints fps (PERF lines 45-135) and the core-0 split.
- Single runs vary about 2% in core-0 samples; compare two runs each, alternating.
- `tools/profile-report.py` fixes (2026-11-11): symbols of `recompiled_*.so` are now read; file offsets are translated through the ELF
  LOAD segments (lld places code 0x1000 above its file offset, which had shifted every name to a neighbour: profiles symbolized before
  this showed `recomp_ginv`/`recomp_aes_sbox` where the memory helpers were); map paths may contain spaces.

## Fast experiment loop (no 2.5 h rebuilds any more)

The setup build keeps every object in `<cache>/kartrecomp/build-work/<module>/{src,obj}` and the link line in `image.link.rsp`.
- `local/exp/relink.sh <recomp_export.c> <out dir>`: one changed `recomp_export.c` (the module's dispatcher), compiled and relinked in
  about 2 s.
- `local/exp/build_main.sh <dir> [jobs]`: recompiles all of `main`'s split units from `<dir>/src` (hot -O2, cold -O0, like the builder)
  and links a module folder: about 5-10 minutes on 5 jobs. Rewrites of the generated C (`local/exp/tools/*.py`) are tested this way
  before they go into the generator (`core/recompiler/arm64_to_c.h`).

## Baseline (release engine + 3.0.4 modules, 2026-11-11)

60 fps throughout except a one-second 54. Core 0 (game main thread), about 31.4-31.9 k samples per 60 s:

| share | what |
|---|---|
| 42-43% | game code (generated blocks) |
| 29-30% | guest memory helpers (`recomp_load64` 12%, `recomp_load32` 6%, `load8` 3%, pairs, stores) |
| 11.6-12.2% | dispatch (`recomp_image_run_slice` 10%, `recomp_lookup` 1.4%) |
| ~9% | engine host code (NVN hooks, kernel/HLE, stripped binary) |
| ~5.5% | libc / vdso |

Where the time inside the hot helpers goes (leaf-IP histogram; a sample lands on the instruction after the stalling one):
- `recomp_load64`: 74% on the read of the guest data itself, 10% on the page-table entry read, the rest in the range checks.
  So the memory helpers are mostly cache misses on the game's own data. A 1:1 native mapping (host address = base + guest address,
  suyu's fastmem arena already exists and is mapped in this build) saves only the table read: about 3-4% of core 0. Inlining the fast
  path into the blocks would also save the call and the checks.
- `recomp_image_run_slice`: more than half of its samples on the read of the block index (8 bytes per guest instruction, about 40 MB
  for `main`), i.e. a cache miss per dispatch.

## Experiments

| experiment | result |
|---|---|
| dc1: shared 8,192-entry cache of recent targets in front of the block index (`recomp_export.c`) | **worse**: core 0 +7% (33.8 k vs 31.4-31.9 k), dispatch 15% vs 12%. The table is written by three game threads and its lines bounce between cores. Dropped (likely also why the earlier dispatch cache was reverted). |
| cc1: chain conditional branches (216 k two-way exits in `main` tail-call their targets instead of returning to the dispatcher) | running |

## Plan (ordered by expected gain per effort; revise with each result)

1. Fewer dispatcher round trips: chain conditional branches (cc1), then a return-address stack so RET goes straight to the block after
   the matching BL (per-thread, small, falls back to the dispatcher on a mismatch). BR/BLR through a per-site inline cache.
2. Inline memory fast path in the generated code, against the 1:1 arena (`base + va`, one bounds check, fallback to the helper).
3. Kernel/HLE layer: measure first (the HLE kernel and services are under 1% of core 0 in this profile; the host share is mostly the
   NVN hooks). Stripping the OS layer is a cleanliness and start-up/compatibility goal more than a frame-time one; do it where it
   removes work per frame (SVC paths per frame, scheduler wakeups).
4. Intel integrated GPUs: needs its own measurement (GPU time per pass at 720p/1080p, render scale defaults, FSR 1 for upscaling,
   shader/pipeline cache warm-up); no Intel hardware here, so build a test path that logs per-pass GPU time and ask the user to run it.
