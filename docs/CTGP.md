# CTGP Deluxe on the static (no-JIT) build

CTGP Deluxe is a Skyline plugin for Mario Kart 8 Deluxe **3.0.3**, which is a 32-bit ARM program. This project's static build is **3.0.4**
(64-bit ARM), and its recompiler only reads 64-bit ARM, so the plugin's code cannot run in it (and there is no JIT here by design). Its
*tracks* are plain game data, though, so the launcher converts them.

## What the conversion does

Mods tab, "Convert CTGP DX zip…": the user's own pack zip becomes the data mod `mods/0100152000022000/CTGP DX (tracks)/`.

* Each CTGP track (`Course/CT_00..CT_3F`) takes the place of one of the game's 47 race courses (cup order: CTGP cup 1 onto the Mushroom
  Cup, and so on; you pick which CTGP cup comes first, 12 of its 16 cups fit). Animal Crossing (four seasonal folders, one name) is skipped.
  The track's files go into the course's romfs folder; its own audio bank (when it has one) becomes `Course_<course>.bars`, with the bank list
  rewritten.
* The thumbnail `ym_CoursePict_CT_xx_00^u.bntx` becomes `ym_CoursePict_<course>_00^u.bntx`; the music `pBGM_CT_xx_N/F.bfstm` becomes
  `pBGM_<course music>_N/F.bfstm`; the cup icons are swapped the same way.
* Order and names: the pack's `CT_xx` numbers follow CTGP DX's cup order (cup k = tracks 4k..4k+3, from its wiki). Its name labels (5001xx) do NOT, so each track's name is found by its text: the layout table (`slots.CTGP_LAYOUT` / `kLayout`) gives the English name of each track and the converter picks the pack label with that text (the same label + 100 is the title form). Tracks that use the game's own names (DK Jungle, Mario Circuit, Rainbow Road) take the game's text. Verified against the user's cup list for the first 12 cups.
* Names: the **game's own** `UI/<lang>/message.sarc` (dumped from the user's game with `KARTRECOMP_ROMFS_DUMP`, see below) is edited: course name
  labels 1401-1496 (+100 for the title form) and cup labels 1301-1312 take the strings of the pack's `CTGPText.msbt` (labels 500100+id,
  500200+id, 700000+cup). The pack's own UI/message files are never copied.
* Nothing is kept from the pack except the converted mod.

Verified: with the converted mod, the cup screen reads "Boo Cup" with CTGP DX's names and thumbnails, race 1 is Omatsuri Circuit
("Taiko no Tatsujin" festival track) at 55-60 fps.

## What is not available

The pack's code: extra cups' own menu pages, the speedometer, the settings screen, online play, custom-music switching, items etc. Doing
those natively means reimplementing them against the 3.0.4 code, a separate (large) reverse-engineering project.

## Tools

* `tools/ctgp/convert.py` is the reference implementation (Python); `app/src/ctgp_convert.{h,cpp}` is the launcher's port (output verified identical).
  `sarc.py`/`msbt.py` are byte-exact readers/writers for the game's message archives (round-trip tested on the real files).
* `KARTRECOMP_ROMFS_DUMP` (suyu fork, `suyu_cmd/suyu.cpp`): with the game loaded (update and mods applied) writes RomFS content and exits:
  `list=<out file>`, `get=<romfs path>=<out file>`, `tree=<dir>=<out dir>[=<only files with this name>]`. Run the game exe with the usual
  `--content-*` arguments.

## Notes for the next person

* Course label numbers are by course id, not cup order; `slots.py` / `kCups` map course folder -> music name -> name label by hand.
* Vanilla course thumbnails are NOT in the dumped RomFS listing, but loose `UI/cmn/menu/ym_CoursePict_<course>_00^u.bntx` files override them.
* The track-to-slot order is simply CTGP cup order; some tracks name a vanilla bank in their `.barslist` (their intended base course), which a
  smarter assignment could use.

## Lessons from the kart-name bug (2026-10-22)

* **Disabling a mod = the config's `DisabledAddOns` list, by name.** Renaming a mod folder (`X.disabled`) does nothing: the emulator loads every folder under `mods/<title id>/`. The launcher now writes the list (`ModAddon::SetModEnabled`, section `[DisabledAddOns]`, entry for title id 72080821221203968: `N\disabled\size`, `N\disabled\j\d=<name>`). Any earlier "disabled by rename" test was meaningless.
* **Dump what the game reads.** `KARTRECOMP_ROMFS_DUMP` first used the loader's raw RomFS, which has neither the update nor the mods: its `message.sarc` was the 1.0 version (265 labels), the running game's has 393 (the update added kart/tire names such as "Master Cycle Zero"). Replacing the game's message file with the old one blanked every name added by the update. The hook now reads the process RomFS (`RomFsController::OpenRomFSCurrentProcess`); run it with no mods (an empty `mods` folder).
* **SARC layout matters:** the game's message archives align every member to 0x80 and the data to 0x100; `sarc.py` / `Sarc::write` reproduce all 13 shipped archives byte for byte.
* A mod that replaces `UI/<lang>/message.sarc` hides every other mod's changes to that file (last one wins); two such mods (CTGP DX and a character mod) cannot both show their text yet.
* Languages: the game has 15 (`CNzh EUde EUen EUes EUfr EUit EUnl EUpt EUru JPja KRko TWzh USen USes USfr`); the converter handles all of them.

## Additive design (2026-10-22): native hooks instead of slot replacement

The user's rule: normal courses are never replaced. CTGP DX adds cup rows with plugin code; this build runs the game's own code recompiled, with a **host-hook seam** (`core/arm/recomp/host_hooks.h`: native code bound to a guest address that observes or replaces the guest function). The additive port is a native mod plugin on that seam, not a Skyline port.

Facts found in the 3.0.4 `main` NSO (decompressed by `scratchpad` helper: segments LZ4, text 0x0..0xB5BE30; load base 0):
* Cup-select screen builder: guest function at **0x4910c8** (called with this=x0). It creates 12 cup-icon objects from the name table at 0x126ae40 (stored at this+0x4c0 + i*8, index at +0x48), then 6 more (`L_CupIcon_12..17`) at this+0x520..0x568 and further members at 0x550... The layout file `UI/cmn/menu.sarc` -> `mn_GrprCupSlct_00.szs` has exactly panes `L_CupIcon_00..17`. Strings: "L_CupIcon_NN" at 0xee556f.., "mn_GrprCupSlct" at 0xee624b (xrefs 0x49404c, 0x4944e0). Screen code region ~0x491000-0x4945xx.
* CTGPDX.nro knows panes `L_CupIcon_18..25`, `cm_Page_Dummy`, `CupIn/CupOut`, `CupIconLineChange_00`, cup name labels 700000+ (table at nro 0x4460).
* Courses: update romfs has 108 course folders = 48 base+DLC1/2, 48 BCP (`Cnsw_11..68`), battle and menu courses. No free slot exists, so CT tracks need NEW course ids: hook where a course id becomes its romfs folder name / music name / picture name and answer `CT_xx` for ids beyond the game's.
Stages: (1) native-mod ABI (`libkr_mod_<name>.so`, install hooks, read/write guest memory); (2) locate the course-id -> folder/name functions (trace by string args); (3) cup screen: extra rows by editing `mn_GrprCupSlct_00` (add panes) or by a host-drawn picker; (4) per-mod enable switch in the launcher.

### Findings, step 2 (course ids), 2026-10-22
* **Course enum** (124 names, comma-separated string at main+0xED2A8D, ids 0..123): 0 Invalid, 1 Gu_Menu, 2 Test, **3..16 `Reserved01..14` (14 unused ids)**, 17..67 the 48 base+DLC1/2 race courses (+Animal seasons), 68..75 battle, 76..123 `Cnsw_xx` BCP. Name getter: guest function main+0x1ACB0 (`id > 0x7b` -> null; lazily builds the 124-entry table). ~55 callers (0x80e1a8.. = BCP music tables, 0x8ab394 = "Course/%s/%s" path formatter at 0x8ab3ac, 0x417214 = "ym_CoursePict_" names).
* Cup screen builder main+0x4910c8 (18 icon members). The cup -> course table is not a plain id array (searched u8/u16/u32 with offsets); needs a dynamic trace.
* **Hook limitation:** `RecompHooks` only sees calls that return through the dispatcher (cross-module). main+0x1ACB0 and its callers are all inside `main`, which the recompiler calls directly, so a plain host hook cannot intercept them. Options: (a) teach the recompiler to emit a hook check at chosen function entries (needs a re-recompile of main, hours; the generator is in `mk8-recomp/src`), (b) patch the generated C of those few functions, (c) hook at module boundaries only (path building goes through sdk functions: the `Course/%s` formatter calls into libc-like sdk code, which may be hookable).

### Memory scanner + cup table search (2026-10-22)
* New debug aid in the fork (`suyu_cmd/mem_scan.h`, env `KARTRECOMP_MEMSCAN=1`): drop a `memscan.req` file in the game folder (commands `u8|u16|u32|u64 <n>...`, `str <text>`, `dump <addr> <len>`, `read64 <addr> [n]`); the running game writes `memscan.out` (3.1 GB mapped, ~60 s per ~25 patterns). Main module load base in the guest = **0x80c8e000** (string `L_CupIcon_00` at 0x81b7356f = base+0xee556f).
* The Mushroom-cup course ids (28,29,20,18) and the 16-id run of cups 1-4 are NOT stored as plain u8/u16/u32/u64 arrays (with offsets 0,-1,-2,-14..-17) anywhere in the 3.1 GB at the title screen; the only u32 hit is the AI rival table (rodata 0x126eb0c, unrelated). The cup -> course mapping is therefore built another way (immediates in code, or created when the cup screen opens). Next: scan again with the cup screen open, or follow the data flow from the cup-screen builder (main+0x4910c8) and `mn_GrprCupSlct` users (0x49404c, 0x4944e0).

### Block tracing (2026-10-22) - how to hook ANY block without recompiling
`SUYU_RECOMP_CHAIN_BUDGET=1` makes every generated block return to the dispatcher, so host hooks (`RecompHooks`) see every block entry, intra-module calls included (slow: analysis only). New fork file `core/arm/recomp/kart_hook_trace.{h,cpp}`: `KARTRECOMP_HOOK_TRACE="1acb0,4910c8,48f000-497000"` (main-module offsets, ranges allowed) logs each entry to `<log dir>/hook_trace.txt` with lr and x0..x7 (+readable strings); `KARTRECOMP_HOOK_TRACE_CAP=<n>` hits per block (default 12). Run scripted: TAS (`tools/make-menu-tas.py --time-trial`, cut at frame 1300 + a `NONE` tail) reaches the cup screen at ~1262 (about 100 s with budget 1).
Findings: cup-screen update function main+0x491674 walks cups in the order table {0,1,3,2,4,5,8,8?,...} (stack at 0x4916a0..) and asks `main+0x491974` for each cup's state (4 normal / 5 / 2 / 0); cup ids 0..11 base, ids 0x12..0x17 for the six extra icons (DLC/BCP; availability via calls to 0x86ff14). The name getter (main+0x1acb0) is only called per-id at startup (all 124 ids, building tables) and with the single current course afterwards; the cup -> course ids are NOT looked up through it, so the mapping is stored/derived elsewhere (not found as a plain id array in memory either). The course-select screen (4 pictures) is the next place to trace.

### Hook-capable recompile (2026-10-22)
Decision (user): the cup table could not be found with static or memory-scan searches, so `main` is rebuilt with a native-hook check in every block. `tools/recomp/add_block_hooks.py <exefs> <new exefs>` inserts into each of the 630,449 generated block functions of `main`
`if (__builtin_expect(g_main_hookmap[off>>2],0) && recomp_block_hook(c, off)) return;` (one byte load + branch when no hook is set; hook map = 4 M bytes in `recomp_runtime.c`). Host: `recomp_block_hook` (end of `arm_recomp.cpp`) runs `RecompHooks::Dispatch` / coverage; `RecompHooks::Install` switches the map byte on (`KartHookTrace::NoteHookAddress`); `KARTRECOMP_BLOCK_COVERAGE=1` switches all bytes on and logs first hits at full speed (no `SUYU_RECOMP_CHAIN_BUDGET=1` needed). The symbol is weak, so the fork still links against generated code without hooks. Compile cost: ~470 s and ~5.3 GB RSS per generated file (33 files) = about 4.3 h with one job. Old sources: `local/main-304b-pre-hooks` (plain copy; exefs-304b/main is now the hooked version, other modules unchanged). The emitter (`core/recompiler/arm64_to_c.h` ~line 5254, after the RECOMP_GG_GUARD line) should emit the same line for future exports - not yet done.

## THE CUP TABLE (found 2026-10-22, hooked build + memscan)
* **Layout:** a heap array of **25 records x 128 bytes**; each record = 8 entries x 16 bytes `{u32 course_index (= enum id - 1), u32 0xFFFFFFFF, u32 name_label (Common.msbt 1401.. / BCP 11701..), u32 0}`; unused entries are all `0xFF` (+ byte 0 at +0xC). Records 0-11 = the 12 base cups (4 entries each), 12-23 = the 12 Booster Course Pass cups (`Cnsw_*`, labels 11701-11748), 24 = the 8 battle courses; record 25 is empty and other data follows, so the array cannot grow in place. Found with `KARTRECOMP_MEMSCAN` `strided 4 16 27 28 19 17` (the ids are stored as **enum id - 1**; my earlier searches looked for the plain ids). A copy of the dump of the first 32 records is `docs/cup-table-dump.txt` (at runtime the table was at 0x214d5e7488 in one run; it moves).
* **Reader (Grand Prix start), main+0x4f7e64..0x4f7f88:** sets race count 4 (`[raceinfo+0x1438]=4`), then for k in 0..3 copies entry k of record `min(selectedCup, 0x19)` (`cup = [raceinfo+0x1d84]`; record address = base + (cup<<7) + k*16, `cmp w9,#0x19; csel ...lo` clamps cup >= 25 to 0) into the race list at `raceinfo+0x143c` (16-byte entries, max 0x30), then calls main+0x41de38 which writes the current course index to `[singleton+0x1a0]`. Race r then uses list entry `[raceinfo+0x1744]`+1 (function around main+0x41dd68). `raceinfo = *(manager+0x30)` where `manager = bl 0x8cb5d4(3)`; the cup table base is also reached through a manager's `+0x30` pointer (4f7f38..4f7f50).
* **Name getter** main+0x1acb0 returns `names[id]` for the *current* course (`singleton->[0x1a0] + 1`), 14 spare ids 3..16 `Reserved01..14` exist.
* **How to add cups without touching normal ones:** (1) build a bigger table in guest memory (host allocation) = the 25 records + CTGP cup records, with CTGP courses given free ids (3..16 aliasing, or ids >= 124 if the name/other tables are extended); (2) hook the table-pointer read and the `0x19` clamp (block hooks) so cup indices >= 25 resolve to the new records; (3) extend the cup screen (`mn_GrprCupSlct_00`, 18 icon panes; builder main+0x4910c8, update function main+0x491674, per-cup state function main+0x491974) with two more rows; (4) route course ids -> folder / music / picture names for the new ids. Steps 1-2 are small; step 3 is the big one.

### Milestone: Grand Prix course list override works (2026-10-22)
`KARTRECOMP_TEST_CUP="16,18,20,21"` (course indices = enum id - 1) installs a host hook at main+0x4f7f7c (the block right after the Grand Prix setup loop; raceinfo = `[sp]`, list at raceinfo+0x143c, 16-byte entries). Picking the Mushroom Cup then ran Mario Circuit, Toad Harbor... (indices 16, 18 confirmed through the course-index store at main+0x41df0c). The cup table itself is never modified. Code: `core/arm/recomp/kart_hook_trace.cpp` (`InstallModHooks`), exported in `patches/suyu/0002` (it also contains the recompiler emitter change).
Next: (1) alias CT tracks onto the spare ids 3..16 (course index 2..15): patch the name table entries (heap table pointed to by `*(base+GOT 0x12f5258)`, in-place overwrite of `Reserved01..14` with `CT_00..`) right after it is built and before the 124-id init loops (the info manager at singleton+0x350 builds picture paths from the names there); (2) converter variant that emits `Course/CT_xx/` folders instead of overwriting game slots, picture `ym_CoursePict_CT_xx_00^u.bntx`; music name for aliased ids still to find (BGM table is indexed by BGM id, 115 entries at 0xee5769); (3) cup choice: F10 overlay "Extra cups" menu arms a cup, the hook swaps the four entries; (4) later the two extra rows on the cup screen.

### MILESTONE: a CTGP track runs from an extra cup without replacing any course (2026-10-22)
Verified in a scripted Grand Prix (Mushroom Cup chosen): with `extra_cup.txt` = `CT_00 CT_01 CT_02 CT_03` in the game folder and the pack's tracks extracted verbatim by `tools/ctgp/extract_cups.py` into a test mods dir (`KARTRECOMP_MODS_DIR`), race 1 is CTGP's Omatsuri Circuit (CT_00) with the normal game untouched. Mechanism: `InstallModHooks` (kart_hook_trace.cpp) at main+0x4f7f7c renames the spare name-table entries (ids 3..6 -> the file's folder names) and writes course indices 2..5 into the race list; the game then loads `Course/CT_xx/`, `pBGM_CT_xx_*` and the picture by name. Not yet checked: races 2-4, the CT music, names/pictures in menus, results screen, saves (trophy records for the chosen cup index). Delete `extra_cup.txt` to go back to normal (it affects EVERY Grand Prix while present).

### Additive pipeline pieces (2026-10-22)
* `tools/ctgp/extract_cups.py <zip> <mod dir> [CT_xx ...|all]`: the 64 CTGP tracks verbatim (`Course/CT_xx`, `pBGM_CT_xx_N/F`, `ym_CoursePict_CT_xx_00^u.bntx`; 4.5 GB, 1020 files), nothing of the game's courses replaced. All 64 are usable (the "16 leftover tracks need DLC" problem is gone).
* `tools/ctgp/messages.py <game message dump dir> <zip> <mod dir>`: per language, the game's Common.msbt gets NEW labels 21000+j (name) / 21100+j (title form) for track j (CT hex number); the 5 tracks that keep the game's names (DK Jungle, Mario Circuit x2, Rainbow Road x2) copy the game's text. `Msbt.add_label` (LBL1 hash group = sum(h*0x492+c) % groups; verified on all 393 existing labels; untouched rebuild is byte-identical). The hook writes label 21000+j into the race-list entry (+8) so the race intro shows the CTGP name.
* `tools/ctgp/cups_check.py <game> <mods dir> <out> [CT_xx...]`: loads each track as race 1 of a scripted GP and screenshots it (CT_00, CT_01 ok so far).
* Overlay row "Extra cup (next Grand Prix)": reads `extra_cups.txt` in the game folder (`Cup name|CT_00 CT_01 CT_02 CT_03` per line), choosing writes/removes `extra_cup.txt`; the overlay deletes a stale `extra_cup.txt` at start-up. `KARTRECOMP_MODS_DIR` points the game at another mods folder.

### Installed + verified in the game (2026-10-23)
* `local/game/mods/<tid>/CTGP DX Cups` (64 tracks verbatim + message files with new labels 21000+j) is installed; the old slot-replacing `CTGP DX (tracks)` is disabled in `[DisabledAddOns]` (not deleted); `!Merged text` was regenerated by the launcher's TextMerge, which now also carries labels a mod ADDS (CTGP's 128 new labels + Vector's 26 changed texts = 154). `extra_cups.txt` in the game folder lists the 16 cups for the F10 "Extra cup" row.
* Race HUD minimap fixed: `UI.bin` is found at the cup screen (1 copy) and the spare indices' 140-byte records are overwritten with a normal course's record when the extra cup starts (verified by screenshot: map on the right side). `KARTRECOMP_EXTRA_CUP="CT_00 CT_01 CT_02 CT_03"` starts an extra cup without the file (tests; the overlay deletes a stale `extra_cup.txt` at start-up).
* Open: music (no entry in the 115-name BGM list for the spare ids), cup-screen rows, trophy/save attribution, race 2-4 of a cup, CT_01 screenshot (showed title art: re-check).

### Music for extra-cup tracks: findings so far (2026-10-23, NOT solved)
* The race music of a normal course is started by the BGM controller object (class at main+0x82bb0 `Start`, which calls main+0x79188; label passed through the generic "start sound by label" function main+0x7997b0 with the label in x2, e.g. `pBGM_WU_FIRST_CIRCUIT_N`). For the extra cup (spare ids) no `pBGM_*` label is ever passed there, only `BGM_COURSE_IN` and `BGM_START_GRID_GP` (verified with `KARTRECOMP_HOOK_TRACE=7997b0`), so the course's BGM id/label is Invalid and the race plays silently.
* The controller identifies its sound by a 64-bit hash object (`[x0+0x1a8]`, fields at +0x10 look like a hash, e.g. e0d2d91df6c32f74), not by the string; the label strings are only registered at start-up by the sound-archive loader (sdk module, 227 `pBGM_*` hits, `KARTRECOMP_STRING_WATCH=pBGM_`).
* Tools added for this: `KARTRECOMP_STRING_WATCH=<prefix>` (dispatcher entries whose x0..x3 point at text with that prefix, with a frame-pointer call chain, `user/log/string_watch.txt`), `KARTRECOMP_TRACE_DEREF` (extra fields for HOOK_TRACE: `0x50` = *(x0+0x50) text, `0x50>0x10` two levels, `i0x1a0` inline text, `I0x1a8>0x10` inline text behind a pointer).
* Ideas: (a) find the function that turns the label into the hash/ID (`0x62c368`?, `0xb4f3c0`) and hook the controller start so the extra cup's `pBGM_CT_xx_N` hash/ID is used; (b) host-side playback of the .bfstm (DSP-ADPCM decode + SDL audio), independent of the guest.

### Music: where it stops (2026-10-23)
Traced with `KARTRECOMP_HOOK_TRACE=79188` + `KARTRECOMP_TRACE_DEREF="0x1a8>0x10,..."`: the BGM controller (object at x0 of main+0x79188/0x82bb0) holds an array of 12-byte-key entries at `*(x0+0x1a8)` (stride 0x20: +0x10 = key of the "_N" label, +0x30 = key of the "_F" final-lap label; each key's HIGH dword is `crc32(label)`, e.g. 0x8660acf1 = crc32("pBGM_WU_FIRST_CIRCUIT_N"); crc32 is main+0x62c51c; the first entry is `BGM_START_GRID_GP` = 0x6416d17f). In a NORMAL race `Start` (main+0x79188, called from 0x82c04 inside 0x82bb0) runs twice (start grid, then the race BGM); in an EXTRA-CUP race it runs only once (the start-grid music): the race-BGM controller is never started (0x82bb0 returns early: `[x19+0x308]` / `[[x19+0x28]+0xa8]` conditions), so setting a key alone will not be enough; the BGM manager's per-race preparation for a valid BGM index has to be reproduced. Next steps: trace the caller chain 0x9eca4 -> 0x9e7d0 (BGM manager state machine, `[x19+0xf8]` = frame counter, `[x19+0x108]` = state) in a normal race and find where the BGM index/resource is bound from the course id; or play the .bfstm on the host (DSP-ADPCM decoder + SDL audio) as a plugin.

### Music: implemented (2026-10-23, needs an EAR TEST by the user)
The atk sound archive only knows the game's own labels, so CTGP's `pBGM_CT_xx_N/F` cannot be started by name. Instead the extra cup borrows eight labels that are never used while racing and points their STREAM FILES at the CT files:
* the per-course BGM database (32-byte records at `[db+0x78]`, count `[db+0xd4]` = 123, pool-relative label offsets: +0 = "_N" label, +0x14 = "_F" label; `db = [[*[GOT 0x12f4d98]]+0xa0]`, pool base = `*[*GOT 0x12f62a8]`) is read by `SetBgm(controller, course index)` = main+0x7ee84. The hook there (spare indices 2..5 only) writes the stand-in label strings into the unused spare records 6..15 (scratch) and points record `idx` at them. Stand-ins: N = `pBGM_ANIMAL_RESULT_{SPRING,SUMMER,AUTUMN,WINTER}`, F = `pBGM_WU_OCEAN_REPLAY`, `pBGM_WU_METRO_REPLAY`, `pBGM_WU_SNOW_MOUNTAIN_REPLAY`, `pBGM_GC_BABYPARK_REPLAY` (result / replay music).
* each stand-in label's stream file name exists once in memory as `"<label>.bfstm"` (sound archive file table); the scan thread (started on the cup screen, same pass as UI.bin) finds them and, at Grand Prix start, overwrites them with `pBGM_CT_xx_N.bfstm` / `_F.bfstm` (`PatchBgmFiles`); `RestoreBgmFiles` puts the originals back as soon as SetBgm is called for any non-spare course (the next normal race). Only memory is changed; no file and no normal course.
* Verified by trace: the game now asks for `pBGM_CT_00_N.bfstm` (string watch) in the extra-cup race. NOT verified by ear; whether the pack's streams play (and loop, and switch to _F on the last lap) is unknown. If a track has no `_F` file the last lap may be silent.
* Side effect to know: until the next normal race starts, replay/result music of Animal Crossing and the four replay stand-ins would play the CT streams.

### Flaky guest crash analysed (2026-10-23): still intermittent, not mod-dependent
"No recompiled block at PC 0x0 ... strict mode - refusing to fall back to the JIT; thread cannot continue" at ~33 s in scripted `-t` Grand Prix runs (seen with mods on, with the old and the hooked binary; the same script ran clean in most other runs; a 6-step mod bisect, `tools/mod_bisect.py`, never reproduced it, so it is timing dependent, not caused by one mod). Crash site: guest function around main+0xDEF00: `x0 = *[[x20+0x38] + x21*8]; blr [[x0]+0x78]` returned a null vtable slot with `x21 = 0xFFFFFFFFFFFFFFFF` (an array index of -1 into a per-player/camera table), call chain main+0x69DE2C <- 0x16B860 <- 0x62E8D8 <- 0x633164 <- 0x625B1C. A defensive host hook at main+0xDEF08 (skip the call when x21 is -1) is the obvious next step; a freeze like the user's "it crashed" reports would match a dead guest thread.
* F10 'Extra cup' row verified with a virtual gamepad (tools: /tmp/ctcrash/vpad2.py, L3+R3, 18x D-pad down, right): writes extra_cup.txt. Never capture the desktop (grim) - use KARTRECOMP_NATIVE_SHOTS.

### CTGP and other mods are exclusive (2026-10-23)
Hand-played races with CTGP's extra cup beside the user's other mods froze (guest crash at PC 0, R21 = -1, "no JIT fallback"); the same selections with all mods off did not. Scripted runs (default driver/kart) never reproduced it. The launcher now enforces it: ticking CTGP offers to turn every other mod off, ticking another mod while CTGP is on offers to turn CTGP off, and Play checks the same. (`MainWindow::resolveCtgpConflict`.) Running `play.sh` by hand skips the check. Which mod causes the crash is not known.

### CTGP paused (2026-10-23, user decision)
The extra-cup route crashed hand-played races (guest PC 0 crash, also with only CTGP loaded), so CTGP DX support is dropped until later. "CTGP DX Cups" is disabled in the game config (files kept in `local/game/mods`), the launcher still has the exclusive-with-other-mods rule. The hooked `main` stays: benchmark of the same 12525-frame recording (native, 1080p): hooked 59.3 fps avg / 52.5 1% low / 10 s below 55, previous build (`mk8d.pre-hookbuild`) 59.5 / 54.0 / 8 s: no measurable cost (within noise).

Active game binary is now the UNHOOKED build (`local/game/mk8d` = old `mk8d.pre-hookbuild`); the hooked one is kept as `mk8d.hooked-final` (ignored by git like all generated output). Swap them back to resume CTGP work.
