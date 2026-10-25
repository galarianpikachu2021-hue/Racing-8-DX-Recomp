# Native GPU: design and progress

Goal: render MK8D by translating its **NVN** graphics API calls directly into a host graphics API
(Vulkan on desktop and Android, Metal on Apple), with no emulated Maxwell GPU. The CPU side is already
native (strict static recomp), so this is the last big emulation layer between the game and the hardware.
It is also what makes an Android/ARM port realistic.

## Where the boundary is (verified ##########)

- The game's `main` module imports a single graphics symbol, `nvnBootstrapLoader`. It's defined in the
  `sdk` module, which carries the NVN driver.
- `main` holds 483 NVN function names. At startup it asks `nvnDeviceGetProcAddress` for each one and keeps
  the pointers. Every later graphics call goes through those pointers, so it crosses from `main` to `sdk`.
- Cross-module calls always come back through the static dispatcher. A host hook at the dispatcher (patch
  `0002`) can therefore observe every NVN call, or replace it.

## Measured API surface (attract mode plus menus, 90 s)

`KARTRECOMP_NVN_TRACE=1` writes `user/log/nvn_trace.txt`:

- 477 entry points resolved, **204 actually called**, 46.7 M calls (about 520 k/s).
- Called functions by object family: CommandBuffer 59, TextureBuilder 20, SamplerBuilder 10,
  Texture/TextureView 18, MemoryPool(+Builder) 13, Queue(+Builder) 14, state objects (blend, color,
  channel mask, depth/stencil, polygon, multisample, vertex attrib/stream) 31, Buffer(+Builder) 9,
  Window(+Builder) 8, Device(+Builder) 8, Sync 3, Program 3, texture/sampler pools 4.
- Hottest: `MemoryPoolMap`, `BufferGetAddress`, `CommandBufferCallCommands` (pre-recorded command
  buffers), `ColorStateSetBlendEnable`, `CommandBufferBarrier`, `BindUniformBuffer`, texture-view setup,
  `BindTexture`, `BindProgram`, and draw calls (`DrawElements` / `DrawElementsInstanced`).

The full trace stays in `local/traces/`. The function list comes from the user's own game, so it isn't
committed.

## Full Grand Prix capture (phase 1, ##########)

The run: 52,905 frames (about 14.7 min, roughly 59 fps with tracing on), strict static, no crash. 207 entry
points were used and 625 M NVN calls recorded, plus 58 frames captured with full arguments (29 samples, 2
frames each). Tool: `tools/nvn-capture-stats.py`.

Per-frame load: menus run about 4–5 k NVN calls and ~100 direct draws. Races run 10–20 k calls and
300–650 direct draws, peaking at about 25 k calls and 980 draws. On top of that come pre-recorded command
buffers (`CallCommands`, about 650 per frame). Course loads spike to 60–70 k calls (resource creation).
Command recording runs on 3 host threads.

Values the backend has to support (from the captures; enum numbering is NVN's, mapping to be confirmed):

| Area | Seen |
|---|---|
| Draws | `DrawElements`, `DrawElementsInstanced`, `DrawElementsBaseVertex` (mode 4, one index type), `DrawArrays(Instanced)` (mode 5), a few mode 0/5 indexed. Compute: `DispatchCompute` (~2 per frame). |
| Render targets | 0, 1, 2, 5, 6 or 8 color targets plus depth (a deferred G-buffer pass with up to 8 MRTs). `ClearColor`, `ClearDepthStencil`. |
| Programs | all graphics stages bound together (bits `0x1f`), compute separately (`0x20`). |
| Resources | uniform buffers on 4 stages (up to 12 slots), textures on 3 stages (up to about 20 slots), 7+ texture formats in steady state (more appear at load time), 2D/array/cube targets, mip levels up to 5. |
| State | blend (one equation; 4 func pairs), depth func (5 values), cull (none/back/front/both), stencil, depth bounds/clamp, polygon offset clamp, channel masks. |
| Sync | `QueueFenceSync`, `SyncWait`, `WindowAcquireTexture` / `QueuePresentTexture` (one each per frame), counters/queries (`ReportCounter`, `ResetCounter`). |

The raw captures and stats stay in `local/traces/gp1-2026-10-18/`.

## Architecture

```
game (static C)  --NVN call-->  dispatcher host hook  -->  nvn-host (our C++)
                                                            |-- object model: Device, Queue, CommandBuffer,
                                                            |   MemoryPool, Buffer, Texture, Sampler, Program,
                                                            |   state objects, Window, Sync
                                                            |-- shader translation: Maxwell -> SPIR-V
                                                            |   (suyu's shader_recompiler, offline + cached)
                                                            |   -> MSL via SPIRV-Cross for Metal
                                                            '-- backend interface
                                                                 |-- Vulkan (desktop, Android)
                                                                 '-- Metal (Apple)
```

Key decisions:

1. **Replace whole object families, not single functions.** NVN objects are guest-memory structs that the
   driver fills in. Once our host code creates an object (texture, pool, command buffer), every function
   touching that family has to be ours, or the guest driver would read a struct it never wrote.
2. **Guest memory stays the source of truth for data the game writes directly.** `MemoryPoolMap` gives the
   game a CPU pointer, and it writes vertices and uniforms there. The host mirrors pools into GPU buffers
   and uploads dirty ranges before each submit (page-protection or explicit flush tracking).
3. **Command buffers are recorded on the host.** `CommandBufferCallCommands` replays handles that our own
   recording produced, so we never need to parse Maxwell pushbuffers.
4. **Shaders:** NVN programs are Maxwell binaries in game memory. Translate each one once with the existing
   recompiler, then cache the result by hash. A shader cache is generated per user from their own game and
   never shipped.
5. **Textures:** block-linear (swizzled) layouts are converted on upload. Render targets stay GPU-only.
6. **Verification:** render the same frames through the emulated path and the native path, and compare
   per draw.

## Phases

| Phase | Deliverable | State |
|---|---|---|
| 0 | Host hook seam in the static dispatcher; NVN tracer | **done** (patch 0002) |
| 1 | Argument-level trace (per call, per frame), frame boundaries, full-GP capture and analysis | **done** |
| 2 | nvn-host object model (shadow) plus the Vulkan backend: own window/swapchain, native render targets, native clears, game's window texture presented | **done** (shadow) |
| 3 | Memory pools, buffers, textures (deswizzle), samplers | in progress: GPU→guest translation done |
| 4 | Programs (shader translation and cache), state objects, draws, so the first real game draw renders natively | in progress: shaders translate (99.9 %) |
| 5 | Pre-recorded command buffers, sync, barriers; a full frame equal to the emulated reference | |
| 6 | Remove the emulated GPU from the build; performance | |
| 7 | Android (Vulkan) port of the runtime; Metal backend | |

## Android / ARM note

The recompiler emits portable C, so the same generated game code compiles for arm64 Android unchanged.
Android needs the runtime (services, input, audio) built for Android plus the Vulkan backend above. Metal
reuses the backend interface with SPIR-V → MSL.

## Phase 2 notes (shadow stage, ##########)

Run with `KARTRECOMP_NATIVE_GPU=shadow`. A second window, "KartRecomp - native GPU (shadow)", opens next
to the game. The report is `user/log/nvn_host.txt`.

- **Object model** (`core/nvn_host/`): built only from observed setter calls. It covers memory pools,
  texture builders and textures, the window (3 textures, presented one is 1920x1080 format 37), command
  buffer recording (with `CallCommands` expanded), submits and presents. The frame count matches the
  game's 60 fps.
- **Copied texture structs:** the engine copies initialized `NVNtexture` structs, so texture objects are
  also decoded from their contents. The layout is pool `+0x10`, offset `+0x18`, flags `+0x20`, target
  `+0x24`, width/height `+0x28/+0x2C`, depth/levels `+0x30/+0x34`, format `+0x38`, storage size `+0x58`
  and GPU address `+0x80`. It's calibrated against the builder on every `nvnTextureInitialize`: 203,032
  matches, 0 mismatches.
- **Render pipeline seen:** 50 render-target shapes. They include a 1920x1080 G-buffer (formats 1, 5, 37, 61,
  63), an HDR downsample chain 960x540 → 30x17 (format 63), 2048x2048 shadow targets (10, 22), and small
  cube/reflection and UI targets.
- **Format numbering** (consistent with every observed use): 1 R8, 5 R16F, 10 R32F, 22 RG32F, 37 RGBA8,
  41 RGBA16F, 46 RGBA32F, 50–54 depth (D16, D24, D32F, D24S8, D32FS8), 56 RGBA8_SRGB, 61 RGB10A2,
  63 R11G11B10F.
- **Vulkan backend** (`suyu_cmd/native_gpu/vk_shadow_backend.cpp`): runs on its own thread and never
  stalls the game (at most 2 queued frames). It creates native images keyed by storage, runs clears, and
  blits the window texture to its swapchain. Measured: 60 fps, 0 dropped, about 365 native images, about
  18 clears per frame. The window is black for now, because the final image is produced by draws.

## NVN driver conventions (learned ######## by pairing NVN calls with the emulated GPU's register state)

Tool: `KARTRECOMP_NVN_RE=1` writes `user/log/nvn_re.txt`. It holds NVN-side lines from nvn-host and
hardware-side lines from video_core for the same objects. They're matched up by GPU address.

- **Shader code:** `nvnProgramSetShaders(program, count, NVNshaderData[])` takes 16-byte entries
  `{u64 gpu_data, u64 control}`. The hardware program (the Maxwell SPH header, then code) starts at
  **`gpu_data + 0x30`**; the first 0x30 bytes are NVN's own prefix. Control sections start with magic
  `0x98761234`.
- **Program region:** the hardware program region base is `0x400000000` in MK8D, which is where the
  shader pool sits.
- **Texture handles:** they live in **hardware constant buffer 2** (`bindless_texture_const_buffer_slot = 2`
  for every draw).
- **Uniform buffers:** NVN slot `n` goes to **hardware cbuf `n + 3`** (0→3, 1→4, 5→8, 6→9, 7→10). Hardware
  cbuf 1 holds driver constants.
- **Stages:** NVN shader stage enum → Maxwell bind group: vertex 0→0, fragment 1→4, geometry 2→3. Compute
  (5) is separate.

## Phase 3/4 progress: GPU memory and shaders (##########)

- **Pool GPU mappings:** an `NVNmemoryPool` holds up to three GPU mappings of its storage, as
  `(descriptor ptr, GPU base)` pairs at `+0x38/+0x40`, `+0x48/+0x50` and `+0x58/+0x60` (pitch,
  block-linear and special views of the same bytes; each is `size` apart). All valid mappings are read at
  `nvnMemoryPoolInitialize` and registered for GPU address → guest memory translation. Cross-checked
  against every texture's GPU address (`NVNtexture +0x80` = mapping + offset): 135,176 agree, 0 disagree.
  GPU-only pools (render targets) have `storage = 0`.
- **Shader translation without the emulated GPU** (`core/nvn_host/shader_translate.cpp`): an
  `Environment` reads Maxwell code from guest memory through the pool mappings and feeds suyu's
  recompiler, Maxwell → IR → SPIR-V. **3,068 shaders translated, 4 failed** (3 non-graphics programs, 1
  per-component interpolation). That's 1,818 vertex, 1,247 fragment and 3 tessellation shaders.
- **Alignment gotcha:** Maxwell puts a scheduling word every 32 bytes, and the decoder locates them by
  address alignment. Code must be addressed from the aligned NVN data address with start offset 0x30.
  Addressing from `data + 0x30` as zero shifts every slot, and about 90 % of shaders then "fail" with
  bogus `FADD CC` / `Token could not be found` errors.
- Still to do for draws: per-draw texture types (`ReadTextureType` currently assumes 2D), vertex input
  state decoding, descriptor layouts from `Shader::Info`, uniform/texture upload, pipelines, render
  passes.

## FIRST NATIVE 3D FRAME (######## night)

`KARTRECOMP_NATIVE_GPU=shadow KARTRECOMP_NATIVE_DUMP=1` dumped the first race frame rendered entirely by
nvn-host + the Vulkan backend: a recognizable lit course in perspective, also present in the game's
final window texture, so post-processing and composition ran natively too. Saved locally as
`local/milestones/2026-10-18-first-native-3d-frame.png`; it's game imagery, so it isn't committed.

Pipeline as built:
- **State objects are engine copies**, decoded from bytes:
  - depth/stencil (8 B): bit0 depth test, bit1 write, bit3 stencil test, bits 4–7 func (1 NEVER … 8 ALWAYS).
  - color (8 B): byte1 = per-target blend enable mask, byte2 = logic op.
  - channel mask (4 B): 4 bits per target.
  - blend (8 B): byte0 target, byte3 = eqRGB | eqA<<4, bytes 4–7 = srcRGB, dstRGB, srcA, dstA.
  - polygon (4 B): bits 0–1 cull, bit 2 front face, bits 3–4 polygon mode.
  - vertex attrib (4 B): bits 0–4 stream, 7–20 offset, 21–26 NVN format.
  - vertex stream (8 B): stride, divisor.
- **Programs are not copied**, so they're tracked by address.
- **Front-face winding must be flipped** (GL y-up → Vulkan y-down). Unflipped, back-face culling removed
  every triangle.
- **Descriptors:** suyu's binding order (per stage: UBO, SSBO, texel buffers, image buffers, textures,
  images) and its push-constant block (RescalingLayout + RenderAreaLayout) are reproduced.
- **Texture pool / sampler pool:** `nvnTexturePoolInitialize(pool, memPool, offset, count)` →
  32-byte hardware TIC entries at `storage + offset + id*32` (MK8D: samplers at offset 0 × 4096, textures
  at 0x20000 × 33068). Handle = textureID (low 20 bits) | samplerID << 20. Textures are decoded with suyu's
  `TICEntry` / `ImageInfo`, unswizzled with `UnswizzleImage` and uploaded; BC formats are native.
- **Debug tools:** `KARTRECOMP_NATIVE_VIEW=grid` (tile the drawn targets), `KARTRECOMP_NATIVE_DUMP=1`
  (PPM dump of the first 300+ draw frame), and `KARTRECOMP_NATIVE_SIMPLE=depth,cull,blend,mask`
  (disable state groups to bisect).

Known gaps: texture types are assumed 2D at translation time (57 k view mismatches); samplers use one
linear/repeat sampler (no TSC decode yet); cbuf 1 (driver constants) reads as zero; pitch-linear and
ASTC textures fall back to stand-ins; the render thread can't keep up while compiling new pipelines.

### Later the same night
- **Texture handles live at cbuf 2 + 0x20** (slot n at 0x20 + 8n; the shader reads slot 0 at 0x20 and slot 1
  at 0x28). Fixing the off-by-4-slots took real textures from about 0 % to 90 %: 664 k uploaded binds,
  250 k native render-target binds, 71 k stand-ins, 947 textures created.
- **Translation-time texture types:** `ReadCbufValue(2, off)` returns the bound handle, and `ReadTextureType`
  decodes its TIC. View mismatches went to 0.
- **The shader recompiler must not run on two threads at once.** Concurrent translation corrupted the heap
  (double free in `SsaRewritePass`), so all translation is serialized by one mutex.
- **Shutdown:** `NvnHost::ShutdownBackends()` is called before `system.ShutdownMainProcess()`. Otherwise
  the shadow window's SDL thread deadlocks process exit.
- **Results (saved locally in `local/milestones/`):**
  - the title-screen logo rendered natively with its real textures;
  - a race G-buffer (normals) rendered natively, a recognizable course.
- **Race lighting is still dark.** Deferred lighting now samples native depth (the depth aspect is
  allowed for non-compare reads). Prime suspect for the remainder: compute dispatches (about 2 per frame,
  likely luminance/auto-exposure) aren't executed yet, so exposure stays 0.

## Overnight ############: from first pixels to full native race frames

Debugging tools added: per-pass dumps (`KARTRECOMP_NATIVE_DUMP=N` writes every pass's first color target
as `passNNN_drawsD_*` plus window and targets), SPIR-V dumps of full-screen passes (`*_spv/`, readable with
`spirv-dis`), and side-by-side reference capture of the emulated window.

Fixes, each found from a pass dump:
1. **Texture handles at cbuf2 + 0x20** (noted above).
2. **Shader recompiler not thread-safe**, so translation is serialized.
3. **SDL window must be created on the main thread.** Creating it on the render thread raced SDL's Wayland
   event queue and corrupted the heap intermittently.
4. **Hardware cbuf 1 = the stage's own NVN shader data blob.** The compiler's literal constants live there.
   Confirmed against the emulated GPU's bind addresses.
5. **GPU reads are piecewise.** Generous cbuf reads that ran past a small pool's end used to fail outright.
6. **Uniform/storage data is snapshotted at submit time** (`ReadGpuMemoryInSubmit`). The render thread runs
   up to a second behind, and the game rewrites its per-frame rings by then; without this, draws got
   another frame's matrices.
7. **`nvnCommandBufferUpdateUniformBuffer`** (1.17 M calls, args `cb, buffer, bufferSize, offset, size,
   data`) embeds uniform data in the command stream. It's captured at record time and applied in order to a
   per-frame shadow seeded from the submit snapshot. This was the cause of the "flat grey" output: the
   tonemap's LUT scale/offset come from it.
8. **Render-target aliasing:** the same memory is used as different same-size formats in one frame
   (RGB10A2 G-buffer → RGBA8). Images are keyed by storage, texel size and dimensions, created
   `MUTABLE_FORMAT`, and given per-format views.
9. **Small CPU-written textures are re-hashed each frame** and re-uploaded on change.
10. **Shadow maps:** depth-compare stand-in cleared to 1.0 (lit) until real cascades are bound.

Orientation: the plain viewport is correct (the title renders upright). 3D passes are upside down in memory
and the next full-screen pass flips them back; that's the game's convention.

**Result:** full race frames render natively through the whole chain (G-buffer, lighting, bloom, tonemap
with the 3D LUT). Saved locally as `local/milestones/2026-10-19-native-race-full-chain.png`.

**Remaining artifacts:** quadrant/offset "fragmentation" from an atlas pass (a 480×270 viewport into a
960×540 target) being sampled without the right UV scale; green tint; the title logo repeats.
Next suspects: uniform values for those passes, and texture views with sizes other than their storage.

## ########## (night): orientation, layered targets, culling, vertex formats

The renderer is a **light pre-pass** design. Each race frame runs:
1. Depth/normal prepass (normal maps only) into an RGB10A2 normal buffer.
2. Light volumes into a **2-layer R11G11B10F light buffer** (diffuse and specular, one layer each).
3. A screen-space sun shadow mask (RGBA8 written over the normal buffer's memory) from a 2048² RG32F
   shadow map.
4. A forward material pass that combines albedo with the light buffer, the shadow mask, baked lightmaps
   and the environment cubes.

Fixes, each traced from a dump:
1. **Viewport flip.** The present transform is 0, so the frame is flipped with a negative-height
   viewport.
2. **Layered render targets.** `NVNtextureView` word1 = base layer | layer count << 16 (valid when word0
   bit1 is set). Images take their array layers from the texture (targets 4/6/8/9), attachments render
   into the selected layer, and clears hit that layer only. Native images can be sampled as 2D arrays,
   cubes and cube arrays. This enables the light-buffer array, the 2-layer shadow map and the GPU-rendered
   environment cubes (128² RGBA16F, 32² R11G11B10F). Before this, all of them were white stand-ins.
3. **Front face.** Ground truth logged from the emulated GPU (`gpu-winding` in nvn_re.txt):
   - Hardware front face is always CCW, viewport scale_y is always negative, origin is upper-left, and
     flip_y is 0. Only the cull face changes (BACK for meshes, FRONT for light volumes).
   - suyu maps this to Vulkan CCW with a negative-height viewport, so NVN polygon bit 2 = CCW.
   - The old inverted mapping was why culling "broke every pass." Culling is now on by default.
4. **Vertex attribute format is ≥ 7 bits** (bits 21..28), not 6. Packed normals use NVN `RGB10A2SN` (105),
   which a 6-bit read turned into RGBA16F (41) on a 4-byte stream: garbage normals for about 500
   attributes per frame. The map now covers 105..108 (10:10:10:2 SN/I/UI2F/I2F) and the UI2F/I2F scaled
   formats (81..104). This is what made lit terrain and roads black. Toad Circuit now renders fully lit
   (`local/milestones/2026-10-19-native-toad-circuit-lit.png`).

New debug switches:
- `KARTRECOMP_NATIVE_PICK=x,y` logs every draw that changes one pixel of a full-size target in a dump
  frame, with its shader addresses, state and texture TICs (`pick` lines).
- `KARTRECOMP_NATIVE_WHITE=w|pfN,...` replaces guest textures of a width, or lightmap-size textures
  (4000+ wide) of a PixelFormat, with white.
- `KARTRECOMP_NATIVE_TEXDUMP=minwidth` CPU-decodes BC1/BC3 uploads to PPM.
- Pass dumps of targets written through an RGBA8 view of a differently typed image are now copied raw
  (`*_asRGBA8`) instead of being blitted with the wrong format.

Lightmaps are BC1 + BC3 atlases (about 4096², level 1 only). BC3 is sampled as a 2D array of a 2D TIC.
The BC1 data decodes cleanly (R/G channels used).

5. **Hardware cbuf 1 = shader data + control[0x700]**, not the data address. Earlier fix 4 (cbuf 1 = the
   shader's own blob) held only where that offset happened to be 0.
   - Found with `KARTRECOMP_TRACE_ALL_FS=1`, which logs, on both the emulated GPU and this backend, the
     constant buffers of the first draw of every fragment shader. Diffing the two (508 shaders) showed
     cbuf 1 bound at data + 0x100..0xb00 on the emulated side.
   - The NVN control section's u32 at +0x700 equals that offset for every shader: the compiler's
     literal constants follow the code, 256-byte aligned.
   - With wrong literals, materials computed nonsense. This was the remaining "black ground": Thwomp
     Ruins, Dolphin Shoals, Music Park and others now render lit and textured
     (`local/milestones/2026-10-19-native-cbuf1-*.png`).

6. **Game samplers (TSC).** Each texture's sampler comes from the handle's sampler ID (bits 20..31) and
   is mapped like suyu's `MaxwellToVK`:
   - GL "clamp" with linear filtering becomes CLAMP_TO_BORDER.
   - No mip filter means minLod 0 / maxLod 0.25; minLod must not exceed maxLod. A first attempt used
     the descriptor's minLod there and blacked out the tonemap.
   - Depth compare uses the TSC function. Anisotropy is used when supported. Border colors go to the
     nearest standard one.
   - The global REPEAT sampler was what tiled the title logo.

   With 1–6, race frames match the game's look: lighting, lightmaps, bloom, exposure, LUT
   (`local/milestones/2026-10-19-native-samplers-*.png`: Hyrule Circuit, Excitebike Arena, title).

7. **Exclusive mode: `KARTRECOMP_NATIVE_GPU=native`.** suyu's renderer is forced to Null: the emulated
   GPU still consumes the command stream (syncpoints, fences) but draws nothing. The native backend is
   the only renderer. Two things were needed:
   - **3D render targets.** The color-grading LUT (8×8×8 R11G11B10F) is drawn once per course in a single
     pass with 8 color targets, each a view of one Z slice (view word1 = slice). Images with NVN target 2
     are created 3D and `2D_ARRAY_COMPATIBLE`; attachments are 2D slice views; sampling uses a 3D view.
     Before this, shadow mode only looked right when suyu happened to flush its own LUT to guest memory
     (racy). Otherwise the tonemap output black.
   - **No dropped frames.** One-time passes (LUT, probes) live in single frames. In exclusive mode
     `Present` keeps every frame and the new `Backend::Throttle()` blocks the game (outside the model
     lock) until at most one frame is queued, like a 2-deep swap chain.

   Result: race frames rendered entirely by nvn-host with 0 dropped
   (`local/milestones/2026-10-19-native-exclusive-*.png`, Tick-Tock Clock). It runs at about 24 fps for
   now, paced by the render thread. Performance is next.

8. **Render-thread performance** (exclusive mode went from about 24 to 60 fps in races). Per-frame
   timers are logged every 300 frames, and slow frames are broken down.
   - **Index scan read back from the upload ring.** The ring is host-coherent (write-combined), and
     reading indices back out of it cost about 100 ms/frame. Indices are now read and scanned in
     ordinary memory, then copied into the ring.
   - **Hashing.** `HashBytes` was byte-wise FNV (about 300 MB/s) and is now CityHash64.
   - **Refresh checks.** CPU-update re-hashing runs every frame for textures up to 16 KB, and every 8th
     frame (staggered) for larger ones.
   - **Persistent `VkPipelineCache`** at `<cache>/kartrecomp/vk_pipelines.bin`. It's saved every 1800
     frames when it grows and at shutdown.
   - **Frame pre-pass.** Each frame's new programs are translated on worker threads; the recompiler is
     thread-safe per call, as in suyu's own pipeline cache. The old global lock covered for the
     statistics worker reading the model without the lock, which is now fixed. New textures are read and
     unswizzled on workers, then created in order.
   - **Large texture uploads use their own staging buffers.** With prefetch, a course's first frame
     uploads hundreds of MB. That starved the 256 MB frame ring, so the one-time LUT pass sampled white
     stand-ins and baked a wrong LUT, which blacked out the whole course.

   Remaining hitch: a course's first frame, about 250–400 ms, mostly cold driver pipeline compiles.
   It's hidden by the intro flyover and shrinks as the cache warms. Steady race frames record in about
   7–14 ms.

9. **ASTC** is decoded to RGBA8 on the CPU in the texture decode step, which runs on the prefetch
   workers, using suyu's `Tegra::Texture::ASTC::Decompress`. Unsupported textures went from about 190
   to 1.
10. **Launcher:** Play tab, *Renderer: Native Vulkan (experimental)*. In exclusive mode the native window
    is the game window ("Mario Kart 8 Deluxe - KartRecomp", 1280×720, honors `-f`). suyu's blank Null
    window is hidden. Input and close events reach suyu's SDL loop, which doesn't filter by window.

11. **Menus and a real race via TAS.** `local/game/user/tas/script0-1.txt` taps A through the menus
    (Grand Prix, 50cc, default racer and kart, first cup), then holds A. Run it with `play.sh -t`.
    `KARTRECOMP_NATIVE_SHOTS=N` saves the presented frame every N frames, and
    `KARTRECOMP_NATIVE_DUMP_MIN=1` lets menu frames qualify for pass dumps and picks. Fixes found this way:
    - **Text everywhere** (menu labels, coin and lap digits). Glyph quads are read by the vertex shader
      from a storage buffer whose descriptor lives in the driver cbuf 0. For graphics stages, NVN storage
      slot k is at c0 + 0x110 + 16k: a u64 address, then a u32 size (verified on the emulated GPU). The
      backend fills cbuf 0 with these and resolves such storage buffers straight from the binds, using
      submit-time snapshots.
    - **Menu movies.** The game decodes them in software and streams a Y plane (R8 512×608) and a UV
      plane (RG8 256×304) per frame with `nvnCommandBufferCopyBufferToTexture(cmd, src, texture, view,
      region{x,y,z,w,h,d}, flags)`. Strides come from `SetCopyRow/ImageStride`. Copies are now commands:
      the source is snapshotted at submit, then copied into a native image that sampling finds first.
      Before this the planes were empty, and YUV→RGB of empty planes is pink.
    - Textures up to 4 MB are re-checked for CPU updates (every 8th frame above 16 KB).
    - Pitch-linear textures are uploaded (rows at the TIC pitch).
12. **Exclusive mode must not persist the Null renderer.** suyu-cmd rewrites its config on exit and at
    other points. The configured backend is restored around every save and before the config's
    destructor, which had silently switched the normal mode to Null.
13. **Compute: the emulated GPU doesn't run it either.** Every compute launch reads an all-zero
    descriptor (grid 0×0×0), so matching suyu's output doesn't need compute. Compute uses storage
    descriptors at c0 + 0x270.. (layout not mapped yet).

14. **Parallel pipeline builds.** The frame pre-pass walks the commands with the same state handling as
    the recording loop. It shares `ApplyRenderTargets`, `ComputeVertexLayout` and `PipelineKey` with the
    real path, so keys match, and compiles missing pipelines on workers (`CreatePipeline` is
    thread-safe; the `VkPipelineCache` is internally synchronized). A course's first frame with a cold
    driver cache went from 825 ms to about 220 ms, now mostly shader translation.
15. **Fidelity check:** shadow mode plus `grim` captures of both windows at once give side-by-side
    emulated/native pairs. Race frames match closely; the emulated window letterboxes.

16. **Programs translated while the game loads.** nvn-host calls `Backend::OnProgram(datas)` on every
    graphics `nvnProgramSetShaders`. A worker translates the program with no textures bound (every
    queried texture slot assumed 2D) and records which (stage, slot)s the translator asked about. At
    first use the backend checks those slots against the real bindings
    (`NvnHost::TextureTypeForHandle`) and rebuilds only on a mismatch (about 8%).
    - A course's first frame went from about 280 new programs to about 13.
    - **Programs are keyed by a code hash, not only addresses.** The game reuses shader memory across
      courses: about 700 replacements in one attract run. Before this, a new course could have drawn
      with a stale translation. Replaced programs and their pipelines are retired (kept until shutdown).
17. **Texture eviction.** Sampled textures unused for 20 s are freed, about 120–180 MB per course.
    Before this, VRAM grew with every course visited.

18. **Resolution scaling** (Display tab's internal resolution). Render targets are scaled like suyu's
    texture cache: single-level 2D/array targets taller than 288 rows, when created as render targets.
    - `Image` keeps the guest size for lookups and matching, and the host size for Vulkan.
    - Viewports and scissors of passes into scaled targets are scaled.
    - The shaders already contain suyu's rescaling pass (it runs when the setting is active). The
      backend fills its push constants: one "scaled" bit per texture element in pipeline order, and
      the pass's down factor.
    - At 2x the window texture is 3840×2160, and race frames stay at about 60 fps on this machine.

19. **Window presentation.** The swapchain follows the window's pixel size. On Wayland the surface
    extent is undefined and resizes never make the swapchain out of date, so the compositor used to
    stretch a fixed 1920×1080 image. An SDL event watch on the main thread publishes the size; no SDL
    calls happen on the render thread. The game image is fitted to the Display tab's aspect ratio
    with black bars ("Stretch to window" fills).

Open:
- A course's first frame is about 150–180 ms, mostly decoding its textures (~110 ms). It's during the
  intro flyover. Next: build pipelines in parallel from a state-tracking pre-pass.
- Thwomp Ruins' water surface was rainbow noise before samplers. Re-check.
- Intermittent startup segfault in suyu's emulated `MasterSemaphore::SubmitQueueTimeline`: 3 of about 15
  runs, at about 4 s, not in nvn-host code.
- ASTC (UI icons) isn't decoded.
- Compute isn't executed.

## ########## (afternoon): first play-test fixes

User report after the first play-test: inverted colors, z-fighting, shadow lines, shadows wrong in general,
missing reflections, sky duplication in replays, dark tracks, missing bloom, the pause background, rumble,
and the info applet.

Fixed:
- **B5G6R5 textures** were mapped with R and B swapped. This caused the blue countdown, FINISH, boost
  flames and cup buttons. MapPixelFormat now follows suyu, with a `FixSwizzle` channel fix-up for
  B5G6R5 and A4B4G4R4.
- **Polygon offset:**
  - `nvnPolygonStateSetPolygonOffsetEnables` (bit 4 = fill) is tracked per state object.
  - `nvnCommandBufferSetPolygonOffsetClamp(cmd, factor, units, clamp)` is captured; decals use -1/-2,
    shadow maps 1/100.
  - The bias is dynamic state (units as given, same as suyu's `depth_bias / 2`).
- **Depth clamp** (`SetDepthClamp`, used by shadow maps) is supported. `SetDepthRange` is honored; MK8D
  only uses 0..1.
- **Native render targets are sampled with their TIC swizzle.** Before this, the swizzle was applied
  only to textures uploaded from guest memory. The shadow mask (RGBA8) is read as (G, B, A, R): green
  holds dynamic (kart) shadows, blue the static VSM course shadows. Without the swizzle, materials read
  a constant red channel instead, so there were no kart shadows, no pad shadow, and characters looked
  flat ("plastic").
  - A detour, now reverted: un-flipping the shadow-map viewport seemed to help, but the emulated
    registers show shadow passes use the same negative scale_y as every pass. It stays only as a debug
    switch (`KARTRECOMP_NATIVE_SIMPLE=smunflip`).
- **Bloom**, three fixes:
  - Native textures use their TIC mip range (`res_min/max_mip_level`).
  - A sampler pinned to one LOD (minLod == maxLod) samples a one-level view, so the bloom chain never
    reads the level it renders.
  - `nvnCommandBufferSetBlendColor` is captured and set as dynamic state; the glare upsampling adds
    each level scaled by a per-level constant.
- **GPU state persists across frames**, as on hardware. Lakitu's cloud was black because its state
  was only ever set in an earlier frame.
- **Pause background**: works in native mode. Shadow mode can drop the one-off frame in which the game
  captures the scene, which leaves the background white there.
- **Integer render targets** get integer shader outputs (`color_output_types`, as suyu does), keyed as
  a separate program variant.
- **GPU-compressed environment probes.** The game renders its 128² cube probes into an NVN DXT5 texture
  (format 68) through an RGBA32UI view (format 47), one texel per block. The backend renders into a
  per-level RGBA32UI proxy and `vkCmdCopyImage`s it into a real BC3 image. Before this the probe was
  missing, so metal and gloss reflections (gears, clocks, guard rails, pad rim) were black.
- **Rumble** is linear (suyu's curve inflated weak effects); `KARTRECOMP_RUMBLE_CURVE` restores a curve.
- **Info button:** a WebBrowser frontend opens the game's offline pages in the system browser.

Testing notes: Smart Steering resets every race. The TAS (`--centre-stick`) turns it on from the pause
menu (+, L, +) after "GO". Side-by-side captures: shadow mode plus grim on both windows.

Still open:
- The kart select pad gets no cast shadow even though the shadow mask has it.
- Sky duplication in replays and flyovers, the pause background, bloom, overall darkness, shaking
  pieces, end-screen textures.

## ########## (evening): effect-heavy slowdown, boost echo

- **Per-frame dedup.** Within one frame, the same guest range (vertex data, constants) and the same
  snapshot are uploaded once. Identical descriptor sets are reused. Particle-heavy frames repeated
  thousands of identical uploads.
- **Model reads without the big lock.** The pool table is a sorted range list under a shared mutex,
  TIC/TSC bases are atomics, and texture IDs have their own reader-writer lock. The render thread
  used to contend with every NVN call the game made. Effect-heavy dips went from about 46 to about
  55 fps; most race frames are 60.
- **Small vertex buffers (≤256 KB) are captured at submit**, like uniforms. Effect and particle
  instance data (the boost "echo" quads, sparks) is rewritten by the CPU every frame, so reading it at
  render time drew effects with the next frame's positions and fades.
- **Snapshot handles on state carried across frames are cleared at frame start.** Since state now
  persists, those handles would otherwise index the previous frame's snapshot.
- **The boost echo** is a 480×270 RGB10A2 copy of a 960×540 blurred frame (pass "set 960x540, then
  480x270 at the same storage"), drawn back on 12 instanced quads with per-instance colour and alpha.
- **New debug switches:**
  - `KARTRECOMP_NATIVE_FS_LOG=1`: fragment shaders drawn per frame.
  - `KARTRECOMP_NATIVE_DUMP_ON_FS=<addr>` and `KARTRECOMP_NATIVE_DUMP_AFTER=<frame>`: dump frames that
    use a shader.
  - `KARTRECOMP_NATIVE_PICK_W=<width>`: pick inside smaller or mip targets.

## ########## (late): replacing NVN recording calls, where the time goes

- **The real NVN driver no longer records anything in native mode.** In exclusive mode, the
  command-recording calls the model handles (draws, state binds, texture, sampler and uniform
  bindings, and so on) return straight to the game (`Replaced()` / `ReturnFromCall`: `pc = lr`). The
  recording calls with no model yet (stencil ref/masks, depth bounds, multisample state, point size,
  line width) are also skipped.
  - Calls that still go to the driver: barriers, fences, counter reports, render enable, copies,
    Begin/EndRecording and CallCommands. The game uses these for its own synchronisation and
    bookkeeping.
  - Shadow mode keeps every call on the driver so the emulated renderer can still be compared.
- **Command lists are shared, not copied.** `CallCommands` appends a reference to the recorded
  list, which is flattened only at submit.
- **Hook dispatch is lock-free.** A 16K-bit prefilter rejects PCs that have no hook, and hook lists
  are shared pointers.
- **Measurements** (1080p, native exclusive, mid-race with effects):
  - Hook time is 2.2–3.3 ms per frame, down from 4–7 ms. Of that, the backend's submit takes about
    1.2 ms (0.36 ms of it is snapshots, about 6 MB per frame) and flattening takes 0.03 ms.
  - The render thread records a frame in 5–8 ms, and the game rarely waits on it.
  - Per-thread CPU over 10 s: CPUCore_0 80%, CPUCore_1 58%, CPUCore_2 52%, main thread 40%.
  - Remaining dips (about 51–55 fps when many effects are on screen) follow the guest's own game
    logic on core 0, which runs the recompiled code, not the renderer. The next target is profiling
    that core's hot recompiled functions.
- The log line `nvn-host submit split per frame` reports flatten and per-backend submit times.

## 2########### (night): is it the hardware, the services, or the CPU code?

Measured mid-race in native mode at 1080p on the user's machine (Ryzen 7 9700X, RX 9070, 15 GB RAM),
using `tools/profile-report.py` and the built-in sampler (`KARTRECOMP_PROFILE=<start>,<seconds>[,wall]`,
written to `KARTRECOMP_PROFILE_OUT`). The sampler takes the interrupted instruction plus code addresses
found on the stack. `perf` isn't installed and `ptrace_scope` is 1, so it runs in-process.

- **Hardware is not the limit.**
  - CPU clocks hold 5.4–5.5 GHz and Tctl stays at 60–63 °C, so there is no throttling.
  - The GPU is 13–15% busy at a low DPM clock.
  - There is no swapping and almost no major faults.
- **Services and "protection" are not the limit.**
  - No service call is hot and none repeats in the log (only one-time startup stubs).
  - In a wall-time profile, the guest cores' idle time is suyu's idle thread, meaning no guest thread is
    runnable: the game is waiting for its next frame.
  - The renderer never makes the game wait ("game waited on renderer 0.00 ms").
- **The limit is the recompiled code on guest core 0.** A frame that needs a bit more than 16.6 ms of
  CPU misses vsync and waits a whole extra frame, which shows up as 52–55 fps seconds. Breakdown of CPU
  samples on cores 0–2:

  | Category | Share |
  | --- | --- |
  | Game code (`blk_*`) | 42–47% |
  | Guest memory helpers (`recomp_load*` / `store*` / `ldp` / `stp`) | 22–30% |
  | Block dispatch and index lookup | ~17% |
  | nvn-host hooks | 2–4% |

  The hot code is spread over about 9,700 functions in 18 of the 33 generated units; the top 200
  functions cover only 28%.
- **Dispatcher loop reads the block index directly** (generator and generated `recomp_export.c`), with
  no call to `recomp_lookup` per block edge. The effect is neutral: the cost is the cache miss on the
  24 MB index, not the call.
- **What would help:** removing that overhead in the generator, which means a re-export and a full
  rebuild of the generated C (about 2.5 h at `-j1`). Options:
  - Inline only the fastmem fast path. Inlining the old full page walk everywhere was measured as 14%
    slower (image 100 → 222 MB).
  - Chain direct branches block-to-block instead of returning to the dispatcher.
  - Keep hot guest registers in locals inside a block.
  - Build for the host CPU (`-march=native`).

## ########: block chaining, effects scenario, what did and didn't help

- **Block chaining is built and verified.** All direct B/BL/fall-through exits tail-call the next block
  (`tools/recomp-chain.py`; the generator emits the same code now). It runs correctly, and the dispatch
  share on core 0 fell from 17.0% to 14.6%, but total CPU use is unchanged, so it's roughly neutral for
  fps. (An early "-11.5%" reading was window-to-window noise.)
- **A scenario that reproduces the slowdown:** `tools/make-menu-tas.py --effects` drifts for mini-turbos
  every 7 s. fps drops to 53–57 in the drift stretches, in both builds. The plain TAS never did that: its
  dips were the pause-menu toggle and a one-off 377 ms hitch at race start (501 pipelines built in one
  frame).
- **Profiler timestamps:** samples now carry a time, so `tools/profile-report.py <file> <bin>
  --seconds=CPUCore_0` shows load per second, and `--hot=CPUCore_0` shows what the busiest seconds
  spend extra time on. Core 0 is 61% busy in quiet seconds and about 80% in effect seconds. The extra
  ~120 ms/s: about a third in guest memory helpers (`recomp_load64` +21 ms/s), a quarter in game code,
  18 ms/s in nvn-host hooks (`Append` doubles).
- **Register caching does not help (tried, abandoned).** `tools/recomp-regcache.py` loads each block's
  guest registers into C locals and writes the changed ones back at every exit. Compiled with the real
  flags on the hottest unit (main_29, 19,030 of 20,000 blocks eligible): code grew 5.7%, instructions
  rose 6.7%, context accesses fell only 8% (168K to 155K) and stack accesses rose 50%. The blocks are
  short and call a helper every few instructions, so values get spilled around each call. It was never
  run in the game, and the tool stays only as a record of the experiment.
- **The memory helper is already tight** (16 instructions; the cost is three dependent loads: context
  field, page-table entry, data). Inlining wouldn't remove them; a software TLB or a host arena
  mapping could, but both need cache-invalidation or fault handling work.
- **CPU pinning helps a little.** `KARTRECOMP_PIN=2,3,4,5` pins guest core N to the Nth host CPU and
  `KARTRECOMP_PIN_RENDER=6` the renderer (launcher: "Pin CPU threads (Linux)", off by default).
  Effects scenario, two runs each: seconds below 58 fps: 14 and 13 of 91 pinned vs 18 and 19 unpinned;
  average fps 59.2–59.4 vs 59.0–59.1. It stays opt-in: with the user's local model also using CPUs,
  pinned threads can't move away from a busy core.
- **A per-thread software TLB for the load helpers does not help (tried, reverted).** One L1-resident
  table lookup instead of the page-table walk, invalidated by a generation counter bumped after every
  page-table write (loads and load-pairs only, opt-in). Effects scenario, two runs each: core 0 used
  44,076 / 44,829 samples with it and 44,271 / 43,677 without; the helper share stayed at about 30%.
  So the page-table entries are cache-hot already, and the helpers' cost is the call and the work
  around it. A helper-only change like this is cheap to try, because the helpers live in
  `recomp_runtime.c` (seconds to rebuild), not in the generated units.
- **Run-to-run noise is about ±0.5 fps and ±5 sub-58 seconds in 91**, so compare two runs per variant.
- **Not yet tried:** `-march=native`/PGO for the generated C (each needs the 2.5 h rebuild and its gain
  is unknown); pre-warming the race-start pipelines; trimming `NvnHost::Append`; letting the game run
  with a higher priority than the user's other programs (needs `gamemode` group membership or
  rtprio/nice limits, which this account doesn't have).

## ######## - Native frame rate (render-side interpolation) - REVERTED

The user judged the visual errors too severe and asked to revert it and look at FSR 4 / DLSS (hardware frame generation)
instead. The prototype is removed from the fork and the launcher; what it taught is kept here.


- Goal: 60-240 fps at normal speed. The sim is a fixed 60 Hz step, so the extra frames are drawn between two
  consecutive game frames: `KARTRECOMP_NATIVE_FPS=<60..240>` (factor = round(fps/60), at most 4).
- How: each frame already carries snapshots of its uniform/storage data. Binds of two frames are matched by
  (op, stage, slot, size) and occurrence (99% match). Words that read as normal floats in both frames and changed
  by less than 35% of their size (+0.02) are blended; everything else (ints, flags, teleports) keeps the new value.
  The changed words are listed once per frame pair; each in-between frame only rewrites that list, renders the
  commands, and presents. Spacing is a fixed 1/fps from a `next_slot` clock.
- What is not blended: passes without a depth target (HUD, menus, post-processing). A frame with no blendable words is
  presented once. The in-between frames skip depth-only square passes (shadow maps).
- First attempt problems and fixes: pacing by the measured game period fed back on itself (game fell to 25 fps);
  per-frame blending cost 1.9 ms (now 0.3-0.7 ms by listing changes once and borrowing the snapshot); recording cost
  4 ms/frame in a race (CPU-bound render thread, GPU only 24% busy) mostly from re-hashing guest texture memory on
  every repeat render (`tex_gen` now advances once per game frame): 2.5 ms/frame.
- Result (180, native, no scaling, TAS race): game 57-60 fps throughout, no dips.
- Menu flicker the user saw came from blending UI uniforms; the depth rule was the fix (never re-checked by eye).
- Lessons for the next attempt: the render thread is CPU-bound (record about 4 ms per frame, GPU about 24% busy), so
  anything that re-renders a frame must reuse the per-frame texture check and uploads; passes without a depth target
  are the HUD/menus/post; the TAS advances one step per presented game frame (Null renderer).
- Idea for hardware frame generation (driver FSR/AFMF style): hand it the UI-free scene and draw the HUD after; the
  depth rule already separates them.

## ######## - Survey for upscaling / frame generation (FSR, DLSS)

What one race frame (1920x1080, native, no scaling) gives an upscaler:

- **Scene colour:** B10G11R11_UFLOAT (HDR), 1920x1080, drawn in one big pass (about 450 draws) plus a second (about 440)
  and smaller ones (particles, effects).
- **Depth:** a full-size depth(+stencil) buffer exists (D32S8-class, the "depth 1920x1080 fmt52" in the pass list).
- **Post:** a bloom chain (960x540 down to 480x272 mips), then tonemap/composite into A2B10G10R10 1920x1080, then the HUD
  pass (about 70 draws, no depth buffer), then the final RGBA8 window image.
- **Missing:** no motion-vector image (the only other small target is one single-channel R16F pass), no jitter, no
  temporal anti-aliasing. So an upscaler would need (a) a sub-pixel jitter added to the camera projection in the
  captured uniform data, and (b) motion vectors made from depth plus the previous and current camera matrices.
  Camera-only motion is the cheap version; moving karts/items would need per-object matrices (the interpolation prototype
  showed those are fragile to match).
- **Where the HUD splits:** passes without a depth target (see the interpolation notes). That is also where an upscaler
  or frame generator should stop: scale/generate the scene, draw the HUD afterwards at full size.

Upscaler options, as checked against AMD's FidelityFX-SDK repo (MIT-style license in docs/license.md):
- v1.1.4 (2025-05) is the last release with a Vulkan backend (FSR 3.1 upscaler, frame interpolation shaders).
- v2.x (2.0 - 2.3.0 "Redstone", which holds FSR 4 ML upscaler 4.1.1 and ML frame generation 4.0.1) lists Vulkan as
  unsupported; on Linux FSR 4 runs through VKD3D-Proton (DX12 over Vulkan) in Proton, not as a native Vulkan library.
- DLSS needs NVIDIA's SDK and runs only on NVIDIA GPUs; it could be an optional untested backend behind the same
  interface (colour, depth, motion vectors, jitter, exposure, reset flag). Don't bundle NVIDIA files.
- The Windows-only third-party "DLSS-NR-on-AMD" tool is DX12-only and not usable here; not touched.

### Step 1 done: camera jitter (#########)

- `KARTRECOMP_JITTER=1` shifts the viewport of every scene pass (a color target plus a non-D16 depth target; shadow
  maps, post-processing, HUD and menus have none or the wrong kind) by a Halton(2,3) offset in [-0.5, 0.5) guest pixels,
  8 phases (`UpdateJitter()` in `vk_shadow_backend.cpp`). A viewport shift moves all geometry exactly like a jittered
  projection, so no shader or matrix changes were needed. `KARTRECOMP_JITTER=fixed:<px>` shifts by a constant, for tests.
- Checked on a race frame: with 0.01 px and with the Halton jitter the picture's brightness matches the unjittered one
  (mean 103.9 / 103.1) and the HUD does not move. A large 6 px shift washed the scene out (an auto-exposure or
  fullscreen-pass reaction), which is outside the real range.
- Still to do for an upscaler: motion vectors (the game has none), rendering the scene below full size, the upscaler
  itself, and keeping the HUD at full size. A camera-only motion vector needs the camera's previous and current
  view-projection (not yet found in the captured uniforms). Per-object motion would need each vertex shader run twice
  (current and previous uniforms), which the translator doesn't do today.

## ######### - Sky duplication fixed: render targets that are mip chains

Symptom (user screenshot, Mario Circuit course-intro title): small copies of the castle and track in the sky at the top
left and top middle, worst in flyovers and replays.

Found by tracing one pixel (`KARTRECOMP_NATIVE_PICK=100,60` in a `KARTRECOMP_NATIVE_DUMP` frame) and the per-pass images:
the scene was clean until pass 26, a fullscreen depth-of-field blend (a fragment shader that reads the depth, a mask
and a half-size blurred copy of the scene at a LOD chosen by distance). That half-size texture (960x540) is built
level by level: pass 24 draws level 0, pass 25 draws level 1 from it. The first description of the texture the renderer
saw said one level, so the image was created with one level, and the render target at level 1 was clamped to level 0
(`min(level, levels - 1)`): pass 25 drew the shrunk scene into the top-left quarter of level 0, leaving three quarters of the
old content, and the DOF blend then sampled that.

Fix: `GrowImageLevels()`: when a render target description says more levels than its image has, recreate the image
with the larger level count (`AllocImageStorage()`, factored out of `GetImage()`), copy level 0 across, queue the old
storage for destruction after the frame (`TextureCopyRetire`). The log line is "image WxH grown to N mip levels"; the
diagnostic "render target ... wants mip level L but the image has K level(s)" shows any remaining case.
Checked on the same intro frame: no copies in the sky, and pass 25's level 1 is now a proper downsample.

Not yet checked: replays, other courses, the user's report of wrong bike wheels on the character/vehicle select screens
(may be a related mip or layer issue), and whether other textures hit the same clamp (the log will say).

Also this session: `make-menu-tas.py --cup=N` (Flower Cup = Mario Circuit, open sky); `KARTRECOMP_NATIVE_SHOTS_RANGE=a,b`
(every presented frame at 480x270); the overlay's speed limit no longer leaks into the saved config (`SdlConfig::SaveAllValues`
writes the configured value); overlay settings persist in `user/config/kartrecomp_overlay.ini`.

## ######### (night): frame drops again, with the architecture in mind

Scenario: `tools/make-menu-tas.py --effects`, native, 1440p, game fps over seconds 60-130 (71 samples), two runs per variant.

- **What the drops are.** Not vsync quantisation: the new `frame pacing:` log line (queue-to-queue intervals, every 600 frames) shows
  the slow frames are 17.5-20 ms, with only a handful of 25-40 ms ones per 600. So a heavy frame needs 1-3 ms more CPU on the game's
  main thread, and the game is simply late by that much. **Adaptive vsync** (`KARTRECOMP_ADAPTIVE_VSYNC=1`: when a vsync found no frame,
  compose the late frame at once and restart the 60 Hz cadence; `Conductor::OnFrameQueued`, `frame_pacing.h`) was built to test the
  theory: it never fired (the starvation condition is not what happens), no fps change. It stays in the tree, off.
- **Where core 0's time goes** (PC histograms with `profile-report` data): `recomp_load64` etc. are about 28% and 78% of that sits on
  the single instruction after the data load, i.e. waiting for the guest's own data (cache misses), not call overhead; the dispatcher's
  self time (8% of core 0) is 59% on one instruction, the load from the 24 MB block index. That is why register caching, a software
  TLB and block chaining were all neutral. A direct-mapped software cache in front of the index (8192 entries, one atomic word per
  entry) was tried in `recomp_export.c`: dispatcher self time went up (8.3% -> 10.7%), core 0 not faster. Reverted.
- **Huge pages for guest memory (kept).** The guest memory is a memfd mapped once (`HostMemory::backing_base`); the existing
  `MADV_HUGEPAGE` was on a placeholder that every `Map()` replaced, so `ShmemPmdMapped` was 0. `madvise(backing_base, ...)` now gives
  about 3.2 GB of 2 MB pages (RSS unchanged, about 5.1 GB). Core 0 samples over the same window: 43,317 / 43,270 without, 42,598 /
  42,229 with (about -2%). `KARTRECOMP_GUEST_THP=0` turns it off. (System setting: `shmem_enabled` must be `advise` or `always`.)
- **Frame generation cost the game 1-3 fps (fixed).** `PresentRealFrame` slept half a frame inside the render thread, so the renderer
  could not start the next frame in time and the game waited on it. The sleep and the present now happen on a `FrameGenPresent` thread
  (the image is drawn at once; `vkQueuePresentKHR` is deferred; the queue is guarded by `queue_mutex`). Game fps with FSR 3 at 100%:
  58.8-59.3 alone, 59.4-59.5 with the built-in frame generation (was 56.5 / 58.1), 58.1-58.4 with FSR 3 frame generation (was
  56.0 / 57.6; its optical flow still costs about 1 fps).
- **Features cost little otherwise**: off 59.1-59.3; FSR 3 DLAA 58.5-58.8; DLAA + HDR 58.8-59.2.
- **Threads in the renderer already exist**: programs, pipelines and texture decoding build on 8-16 worker threads, and `NativeSpec` compiles
  programs speculatively; nothing to add there.
- **Test aids**: `KARTRECOMP_HITCH=<period>,<ms>[,<from>]` (sleep in the renderer), `GPU frame time` log line, `--time-trial` TAS.

## The "PC 0" race crash (investigation, #############)
Symptom: a guest thread dies with "No recompiled block at PC 0x0 ... strict mode" (game freezes, process stays alive). R21 = -1, R00 = 0, LR = main+0xDEF24. Seen on gcc builds (hooked and unhooked, user's interactive races with mods: 3/3, CTGP only: 1, no mods: 0/1; scripted runs: occasionally) and once at 36 s of the benchmark replay on the clang hot/cold build (not on the next 2 runs of that build): timing-dependent.
Code (3.0.4 main, from the generated `_expected` words):
```
deef0: mov x22,x19 ; ldr x20,[x22,#0x30]! ; mov x0,x20 ; ldrsh x21,[x22,#0x854] ; mov w1,w21 ; bl 0x69d48c
def08: ldr x8,[x20,#0x38] ; ldr x8,[x8,x21,lsl#3] ; ldr x0,[x8] ; ldr x8,[x0] ; ldr x8,[x8,#0x78] ; ldrsh w1,[x22,#0x856] ; blr x8
```
The signed 16-bit index at object+0x884 is -1 ("none"), the array lookup at index -1 yields a null object, the virtual call jumps to 0. The gate before (deed4) tests a state value against bit mask 0x2085a00c2. One direct writer found: `strh` at block main+0x71dc10 (`[x21+0x884]`); stores through other bases / wider stores not searched yet. Hypothesis: load timing (worse with many mods, slower code): the reader runs before the writer on our scheduling. **Root cause (#########): a character mod, not the recompiler.** The index is a driver-skeleton BONE index: the writer (main+0xd5fac..0xd6054) calls the bone lookup main+0x69c6a0 with names from rodata (`Hair1`, `Hair`, `HairL`/`HairR`, `Crown1`, `Flag`, ...) and stores -1 when the model has none of them; the reader assumes the bone exists for that character's hair/accessory type. With the user's mods (character model swaps) the replay crashed 2 of 8 runs (the CPU opponents are random, so only races with the swapped character crash); with `KARTRECOMP_NO_MODS=1` 0 of 6. Not fixed (a guard at main+0xdeef0 would have to supply a valid matrix: main+0xdef24 reads [x0+0x30]); tests run without mods.
