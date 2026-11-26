# Shader preparation at install time

The first time a race draws something new, the native renderer translates the shader and builds a Vulkan pipeline in the
middle of the frame. On a PC whose driver has never seen these pipelines, that is a visible freeze (water entry and exit on the
second course of the Mushroom Cup, item effects, the first lap of every course). Each pipeline is built once: after that the game
cache (`vk_pipelines.bin`) and the driver's own cache serve it.

These caches belong to one GPU and one driver, so they cannot be shipped. What can be shipped is the **list** of the pipelines the
game makes: for each one, where its shader code sits in the game's files and the state it was built with. `kartrecomp-setup`
gives that list to the engine after the test boot, and the engine builds every pipeline into the game's cache before the first
race. The list holds no game bytes.

## In the setup

`data/mk8d-<version>/pipelines.txt` next to the hot block list turns the step on. After "Testing the game", the setup starts the
engine the way the launcher starts the game (same settings, same cache folder, native renderer) with:

- `KARTRECOMP_PRECOMPILE=<list>`: the engine opens a hidden window, creates the list's pipelines, writes its cache and exits
  without loading the game. It prints `PROGRESS <permille> <text>` lines, shown in the setup's progress bar (990 to 999).
- `KARTRECOMP_PRECOMPILE_EXEFS=<export>/nso`: some shaders are inside the game's executable, not in its RomFS. The engine reads
  them from the exporter's NSOs, so the step runs before "delete the build files".

The step never fails the build: without Vulkan, or if the engine stops, the game still runs and compiles as it goes. The engine's
summary line (`precompile: N programs, M pipelines (F failed, S skipped)`) and a `precompile details:` line are in its log.

## The list

First line `# kartrecomp pipelines 1 <game version>`, then one `P ...` line per pipeline. The recorder's format is described
above `RecordPipeline` in `vk_shadow_backend.cpp`; the list replaces each stage's `<stage> <size> <sha1>` with
`<stage> <size> <sha1> <romfs path> <offset>`, where the path is the file holding the code (`<file>@0x<offset>` for a Yaz0
stream inside it, `exefs:main` for the executable) and the offset is that of the shader's 0x30-byte NVN header in the
decompressed file. The engine checks each stage's SHA-1 before using it: a different game version or a modded file only skips
the pipelines it affects.

Recording the list again is needed after a change to what the engine puts in a pipeline (its state, the translation, the
renderer's own passes), not after a change to the game code build.

## Recording a list

1. **Record.** Run the game with `KARTRECOMP_RECORD_PIPELINES=<file>` and play what players play. Every pipeline the renderer
   creates is written once to `<file>`, and each stage's exact code to `<file>.code/<sha1>.bin`. A TAS that plays the 12 Grand
   Prix (`tools/make-menu-tas.py`) covers the courses; the cups' award ceremonies need a podium (50cc).
2. **Dump the RomFS** of the same game version, update applied, with no mods:
   `KARTRECOMP_ROMFS_DUMP=tree==<out dir>` (see docs/CTGP.md).
3. **Index it and build the list** with `tools/pipeline-list/` (README there):
   ```
   shader_index index <romfs dir> <romfs.idx>                       # SHADER_INDEX_KEYLEN=0x80 for the short-key index
   python -I build-list.py <recording> <romfs dir> <k80 index> <out list> <version> --exefs <cache>/export/nso
   python -I build-list.py --verify <out list> <romfs dir> --exefs <cache>/export/nso
   ```
   Several recordings can be concatenated (with their `.code` folders merged): identical lines are kept once.
4. Put the list at `data/mk8d-<version>/pipelines.txt`.

The `q` entries of a line are the texture handles the translation read and the texture type each one had. The precompile only
uses the type, so two lines that differ only in handle values build the same pipeline: `build-list.py` keeps one of them.

## Measured

All on Windows (7800X3D, RTX 3070 Ti, NVIDIA driver cache already warm), 4.0.1.

| | |
|---|---|
| Recording | the 12 Grand Prix at 200cc and cups 1 and 2 at 50cc up to the ceremony (engine of 2026-11-23), plus cup 1 at 200cc on this branch's engine (upstream main + this patch) |
| List | 5,498 pipelines, 527 game files; 1 recorded pipeline dropped (a stage of unknown size) |
| `--verify` | every stage found, 0 SHA-1 mismatches |
| Without the `q` dedup | 12,992 lines; preparing them on top of the deduplicated cache adds 0 bytes |
| Setup, whole build | `kartrecomp-setup` from this branch, empty cache: 3 min 52 s, of which "Preparing the shaders" 10.3 s (5,489 pipelines, 0 failed, 0 missing files) |
| Coverage | a new cup 1 run on this engine created 1,491 pipelines: 1,482 were in the 5,489-line list. The 9 others are spread over the Grand Prix, 7 of them with shaders no earlier recording had (item and character effects vary from run to run); they are in the list now |

Cup 1 Grand Prix by TAS on this engine, pipeline creation time per race (sum over the slow frames):

| race | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| with the prepared cache | 5 ms | 31 ms | 280 ms | 11 ms |
| without a game cache | 27 ms | 60 ms | 10 ms | 66 ms |

Race 3 with the prepared cache had three frames of about 90-100 ms building 2-5 pipelines each: the effects that were not in
the list yet (see Coverage). Without a game cache those pipelines were cheap here only because the driver's own cache already
had them. On an earlier engine, the Mushroom Cup's second course (water) went from 5 frames compiling in the race to 0.

**Not measured yet:** a PC whose driver never saw these pipelines (where each costs tens of ms), and Linux: the steps are
platform neutral, but nothing here ran on Linux.