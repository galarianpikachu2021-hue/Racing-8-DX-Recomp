# Native performance project (branch `native-perf`, local)

Goal (user, 2026-11-11): make the CPU side fully native, strip the emulated console OS layer down to what the game needs
(LASRecomp-style static program), keep the testing loop fast, and run well on Intel integrated GPUs (laptops, Windows and Linux).
Strict rule unchanged: no JIT anywhere.

## Measuring

- Scenario: `tools/make-menu-tas.py --effects` race, `KARTRECOMP_NO_MODS=1`, native renderer without scaling, profile from 60 s for
  60 s (CPU mode). `local/exp/ab.sh <label> <module dir>` runs one race with the release engine (`local/game/mk8d-engine`, v0.2.3,
  stamp `d201f1ec0e-abi6`) and a module folder, restores the configs, and prints fps (PERF lines 45-135) and the core-0 split.
- Run-to-run noise is larger than first thought: nc1 alone gave 30.2, 30.2, 30.7 and 32.5 k core-0 samples (computer karts and items
  differ per race). Whole-run totals only resolve changes above about 3%; the per-category shares are steadier. Use 3+ runs each.
- Caveat: the `--effects` TAS wedges the player's kart on the wall after the first turns in every build (shots at frames 5400-8100),
  so the profile is 11 computer karts plus a stuck player. Valid for A/B, but fix the TAS before quoting absolute numbers.
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
| cc1: chain conditional branches (216 k two-way exits in `main` tail-call their targets instead of returning to the dispatcher) | **neutral**: core 0 31.1 k / 32.3 k vs 31.4 k / 31.9 k; dispatch share 12% -> 7.4%, but game code rose by the same amount (the block-index miss is replaced by misses and mispredicts in the chained code). Same outcome as the old block chaining. |
| nc1: on top of cc1, BL as a native C call with a guarded direct continuation (176,702 BLs in `main`, depth counted in the unused `fm_reserved` field, capped at 48), so a RET does not go back to the dispatcher | **better, -4.5% core 0**: 30.19 k / 30.20 k vs baseline 31.4 / 31.9 / 31.5 k; dispatch 6.2-6.5%. Fully static, 60 fps, same picture and log errors as the baseline. |
| im1: on top of nc1, the plain loads' fast path inline in the hot (-O2) pieces only (an older whole-image inline was 14% slower from code growth) | **no gain**: 30.4 k / 31.6 k vs nc1 30.2 k / 30.2 k; helpers drop to 8% but game code rises to 70%: the stall is the guest data, wherever the load sits. Dropped. |

## Plan (ordered by expected gain per effort; revise with each result)

1. Fewer dispatcher round trips: chain conditional branches (cc1), then a return-address stack so RET goes straight to the block after
   the matching BL (per-thread, small, falls back to the dispatcher on a mismatch). BR/BLR through a per-site inline cache.
2. Inline memory fast path in the generated code, against the 1:1 arena (`base + va`, one bounds check, fallback to the helper).
3. Kernel/HLE layer: measure first (the HLE kernel and services are under 1% of core 0 in this profile; the host share is mostly the
   NVN hooks). Stripping the OS layer is a cleanliness and start-up/compatibility goal more than a frame-time one; do it where it
   removes work per frame (SVC paths per frame, scheduler wakeups).
4. Intel integrated GPUs: needs its own measurement (GPU time per pass at 720p/1080p, render scale defaults, FSR 1 for upscaling,
   shader/pipeline cache warm-up); no Intel hardware here, so build a test path that logs per-pass GPU time and ask the user to run it.

| ci1: on top of nc1, a 4-byte compact copy of the block index (20 MB instead of 40) | **no gain** (30.6 / 30.5 k). Dropped. |
| np1 (engine, fork `arm_recomp.cpp`): the per-dispatch shared atomic `early_dispatches` add (2.4% of core 0, `lock xadd` bouncing between three threads) now stops counting after 64; the per-dispatch FP-mode read (`stmxcsr`, ~1.9%) only after entry, a serviced SVC or a host hook | engine share of core 0 9.4% -> 7.7% in all three runs; totals 30.7 / 30.7 / 30.6 k vs nc1 30.7 / 32.5 k in the same session. Kept (in patches/suyu on this branch). |

Huge pages for guest memory are active in the engine build (log: `MADV_HUGEPAGE on the 4096 MB backing mapping: ok`).

## Conclusions so far (2026-11-11)

- The game's main thread is bound by cache misses on the game's own data (about 20% of core 0 sits on those loads) and on the
  dispatcher's block index. Removing instructions around them (inline helpers, a 1:1 mapping, conditional chaining) moves the stall
  instead of removing it.
- What does help is not touching cold data at all: native calls (nc1) skip the dispatcher and its index read on every RET
  (dispatch 12% -> 6.4% of core 0; 2-4% net, within the noise of single runs), and the engine fixes (np1) drop 1.7 points.
- The emulated OS layer is cheap per frame here: HLE kernel/services under 1% of core 0, the engine's own code 8-10% (NVN hooks,
  the dispatch loop). Stripping it is worth doing for simplicity and start-up, but it will not give a large frame-time gain on its own.
  Next in the same spirit: BLR/BR through a per-site cache of the last target (no shared lines), and a compact block index
  (4-byte entries, or an index of block starts only) so the remaining dispatches touch less memory.
- nc1 goes into the generator (`ChainTo` for BL in `core/recompiler/arm64_to_c.h`) behind an exporter switch, default off until it
  has had a full play test, then on with a new module stamp (users rebuild their game once).

## Intel integrated GPUs: shader capabilities from the device (np2, 2026-11-11)

The native renderer translated every shader with a fixed desktop profile (64-bit floats, fp16, int8/16/64, full float controls,
typeless image loads, subgroup vote), never asking the GPU. Intel Xe integrated GPUs (Tiger Lake and later) have no 64-bit floats,
so any shader using them would fail to build or misrender there. Now `core/nvn_host/shader_translate.{h,cpp}` has
`ShaderDeviceCaps` (defaults = the old assumptions) and the Vulkan backend lowers it from the real device right after creating it
(`shaderFloat64/Int64/Int16`, `shaderFloat16/Int8`, float-control properties, demote, formatless storage reads, subgroup vote), with
one log line: `nvn-host vulkan: shader caps fp64=... fp16=...`.
- On this AMD card (RADV) every capability is present except independent denorm/rounding modes (RADV reports them as not
  independent): the old profile claimed those too, so the new one is also more correct on AMD. Race fully static, 60 fps, same errors.
- Test switch `KARTRECOMP_SHADER_CAPS_OFF=fp64[,fp16,int8,int16,int64,float-controls,demote,typeless,vote]` simulates a smaller GPU
  here. With `fp64` off (Intel Xe's profile) the race renders correctly (screenshots checked) at 60 fps.
- Still to check on real Intel hardware (none here): driver compatibility of the present path and HDR, GPU time per frame at
  720p/1080p, sensible defaults (render scale, FSR 1) for laptops. The Vulkan requirement itself is met by Intel's Mesa (ANV) and
  Windows drivers on Gen9 and newer (Vulkan 1.3, dynamic rendering).
