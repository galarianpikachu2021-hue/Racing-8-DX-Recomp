# CTGP Deluxe on 3.0.4 / 4.0.1: what the original does, and how it is recreated

CTGP Deluxe 1.1.1 is a Skyline plugin (`romfs/skyline/plugins/CTGPDX.nro`, 32-bit ARM) for MK8D **3.0.3** (32-bit). It cannot run in
this project's 64-bit builds (3.0.4, 4.0.1), so it is recreated: the engine applies the same changes to the 64-bit game code as host
hooks, at the 3.0.4 / 4.0.1 addresses of every place the plugin patches in 3.0.3. The plugin's data (tracks, texts, UI files, its course
and cup tables) comes from the user's own pack at import time; nothing from the pack is in this repository.

Study tools (all in `tools/ctgp/`, outputs go to a scratch folder):

| tool | what it does |
|---|---|
| `nx2elf.py` | NSO / NRO (32 or 64-bit) to a section-less ELF that Ghidra loads with its dynamic symbols |
| `ghidra/ExportDecomp.java` | headless post-script: decompiled C of every function (or of address ranges) + symbol list |
| `ghidra/HookSites.java` | lists every call to the plugin's hook/patch wrappers with constant-folded arguments (the patch list) |
| `ghidra/FuncFeatures.java` | per-function features (callees, strings, externals, immediates) for cross-build matching |
| `match_funcs.py` | matches functions between builds (3.0.3 32-bit -> 3.0.4 / 4.0.1 64-bit) from those features |
| `sites.py` | per patch site: the 3.0.3 instructions and the plugin's handler stub, disassembled |
| `emu_plugin.py` | runs the plugin's start-up code in the unicorn emulator and dumps the tables it builds (`coursedb`, `boot`) |

Game code: `KARTRECOMP_EXPORT_NSO_ONLY=1 kartrecomp-export --base ... --update <3.0.3|3.0.4|4.0.1> --keys ... --out <dir>` writes the
update's ExeFS NSOs (any version, 32-bit too).

## How the plugin works

`main` (nro 0x200) builds its tables, then installs its patches through five wrappers (nro offsets):

| wrapper | Skyline call | meaning |
|---|---|---|
| 0x2fe0 `(fn, handler)` | `A32HookFunction(main+fn, handler)` | replace a whole game function |
| 0x3008 `(fn, handler, off)` | `A32InlineHook(main+fn+off, handler)` | run `handler` in place of the instruction at fn+off |
| 0x3030 `(fn, handler)` | same with off 0 | |
| 0x3038 `(addr, word)` | write | overwrite one instruction |
| 0x305c `(fn, off, word)` | write | overwrite the instruction at fn+off |

Inline handlers are hand-written assembly stubs: they repeat the replaced instruction, save registers, call a C++ function of the plugin
and put its result into a register, then return to the next game instruction. In total: 7 function replacements, 160 inline hooks,
88 instruction overwrites, at about 110 distinct game functions (full list: `HookSites.java` output, annotated by `sites.py`).

## The plugin's data (from the emulator; `emu_plugin.py ... coursedb|boot`)

### Course database: 188 entries = the game's 124 course ids + 64 new ones

The game's course enum has ids 0..123 (0 Invalid, 1 Gu_Menu, 2 Test, 3..16 Reserved01..14, 17..67 base/DLC, 68..75 battle, 76..123
BCP). CTGP appends **CT_00..CT_3F as ids 124..187** (course index = id - 1 = 0x7b..0xba). Entry layout (0x40 bytes, 32-bit):

| off | field |
|---|---|
| 0x00 | folder name (`CT_00`), used for `content://Course/<name>/` |
| 0x04 | picture name (`CT_00_00` -> `ym_CoursePict_CT_00_00^u.bntx`) |
| 0x08, 0x0c | 16-byte values per course (hashes; the plugin checks `course_kcl.szs` / `course_muunt.byaml` for online) |
| 0x10 | course index (id - 1) |
| 0x14 | **base course index**: the original course this track "behaves like" wherever the game has per-course code or tables |
| 0x18 | name label (game: 1401.. / 11701..; CTGP: 500100+n in `CTGPText.msbt`) |
| 0x1c | platform label ("SNES", "GP", "DKR", ...: game 1461..; CTGP 500000..500005), -1 = none |
| 0x20 | slot in its cup (0..3) |
| 0x24 | track author label (600000+, `CTGPText.msbt`) |
| 0x28 | BGM course index (whose music settings to use) |
| 0x30..0x33 | bytes; +0x33 = cup id (-2 = custom) |
| 0x34 | flag byte (3 on some tracks) |
| 0x38, 0x3c | music variant lists |

### Cup database: 40 cups

Cup ids 0..11 = the 12 base cups, 12..23 = the 12 Booster Course Pass cups, **24..39 = the 16 CTGP cups** (Boo, Blooper, Penguin
Suit, Piranha Plant, Cloud Flower, Super Crown, Grand Star, Multi Moon, Invincibility Leaf, Squid, Heart, Shine Sprite, Life Mushroom,
Ice Flower, Prankster Comet, Coin), four tracks each in CT order (cup 24 = CT_00..03, ..., cup 39 = CT_3C..3F). Cup object:
`{count, name label (1301.. / 11601.. / 700000+n), icon name ("Boo" -> UI/cmn/race/ym_CupIconBoo_*.bntx), entry*[4], ..., cup id}`.
In the game, record 24 of the cup table is the battle "cup"; CTGP's cup ids 24.. overlap it, which is one reason for the many hooks
on cup and course indices.

### Texts (`CTGPText.msbt`, per language)

500000.. platform names; 500100+n / 500200+n track names (plain / title form); 600000+ track authors; 610000.. lap variants
("Normal", "Second", "Final"), 610100.. music tags ("(Remix)", seasons); 620000.. music artists; 630000.. song names; 700000+n cup
names; 800000.. music sets ("CTGPDX OST", "Author's Pick"); 810000.. settings pages and rows; 813000 "Settings".

## Features (by patch group)

Group = the plugin's installer function (nro offset). Site counts include function replacements and instruction overwrites.

| group | sites | feature |
|---|---|---|
| 0x8ccc | 74 | **course ids beyond the game's**: every read of the current course index (`[raceSingleton+0x1a0]`, getter main+0x906fb4) in code with per-course behaviour returns the entry's *base course* (0x14); name / picture / author / BGM-index lookups go to the database; tables indexed by course (0..0x7a) get 0x10 for new ids |
| 0x94c4 | 54 | course-count immediates (`cmp #0x7b` and table sizes) set to 188; input-mask changes on the cup screen (L/R paging); object counts |
| 0x84a0 + 0x80ec | 35 | **online / local play**: course ids in packets are 7 bits (`& 0x7f`); widened to a 32-bit field so ids > 127 survive; matchmaking key `MarioKart8Delux` -> `CTGPDelux010101` (separate player pool) |
| 0x84d4 + 0x8634 | 12 | **speed multiplier** (physics `1/x` scaling sites) and race timer limit |
| 0x5538 | 6 | **cup select screen**: two more rows of icons (`L_CupIcon_18..25`), page scroll animations (`CupIconLineChange_00`, `Scroll_RL/LR`), L/R buttons |
| 0x86ec | 11 | title / mode menus: CTGP **Settings** page (`N_Setting_00`, rows from 810000..) |
| 0x8644 | 4 | music switch on the course screen (`L_MusSwch_00`, song name / author / lap variant texts, sound-test SE) |
| 0x53d8 | 9 | file redirects (course files by database name, music stream names `pBGM_<course>_{N,F,LAP2,START_GRID,GOAL,RESULT,REPLAY}`), course-file presence checks |
| 0x562c | 1 | path redirect (music streams) |
| 0x95fc + 0x9730 | 24 | random track choice over the extended database; misc function replacements |
| 0x6a7c | 3 | honk visibility / speedometer HUD flags |

## Recreation plan (engine side)

1. **Block roots at every site**: the exporter gets a per-build list of extra block starts (`DiscoverBlocks` already takes
   `extra_roots`), so a hook can sit on any instruction, not only on block starts. Block hooks (`SUYU_RECOMP_BLOCK_HOOKS`) then cover
   them; a hook may change registers/memory and continue, or set `pc` and return 1 to skip instructions (= the plugin's stubs).
2. **Address tables**: each 3.0.3 site -> its 3.0.4 and 4.0.1 address (function matching with `match_funcs.py`, then the instruction
   inside the function by its role), kept as data next to `kart_hook_trace.cpp`'s `GameAddrs`.
3. **Tables from the user's pack**: course and cup databases and texts extracted at import (emulator logic reimplemented in C++, or a
   static reader of the builder at nro 0xb010), stored in the mod folder.
4. Features in order of value: course ids + cups + cup screen rows -> music -> settings / speedometer / timer -> online.

## Status (2026-11-18)

Working on 3.0.4 (scripted runs; 4.0.1 has every address but is not run yet):
* Course table, cup table and texts from the user's pack (launcher import, `ctgp_tables.txt` + `CTGPText.msbt`).
* CTGP's 64 tracks as new course ids 124..187: names, platform tags, pictures, base-course behaviour, music streams
  (`pBGM_CT_xx_*`), course-local MapObj files, per-track SpeedMultiplier, no time limit.
* Cup screen: CTGP's layout (two extra icon columns, L/R line scroll with `Scroll_RL/LR`), 8 extra CupIcon parts created through
  game calls from the hooks, the plugin's remap, every cup -> course lookup remapped (Grand Prix list, course preview, labels).
  Verified: R twice -> purple CTGP cups; Boo Cup shows Omatsuri Circuit / Vanilla Lake 1 / Broken Pier / Mystic Tangle and races
  Omatsuri Circuit at 60 fps.
* Engine mechanisms: block starts at every site (`--roots`), hooks on exact instructions, calling game functions from a hook
  (`CallGuestSeq`: registers saved, return trap, resume; a resumed block passes the hooks twice), `KARTRECOMP_CTGP_SKIP=<handlers>`.

Known limits / next:
* Extra icons on the first cup page only (Grand Prix): the 64-bit menu heap has no room for a second set (the plugin itself
  skips its third page). VS / Time Trial cup pages show CTGP cups only through ... nothing yet (no paging there).
* Menu background / title screen / top menu layouts left out: CTGP's 3.0.3 `mn_Background_00` makes 3.0.4 call
  `nn::os::LockMutex(null)` on a loader thread.
* Speedometer: `T_SOM_00` is in the HUD (shows the layout's placeholder); needs the local kart pointer (3.0.3 path
  raceSys+0xf0 -> +0x12c -> +0x68 -> [i] -> +4 -> +0x14; speed field 3.0.3 +0x330 = 64-bit +0x37c, found by matching).
* Music switch / now-playing banner (`L_Sus_00`, `L_MusSwch_00`), CTGP Settings page (title button, page 0x88 in 4.0.1),
  race timer, honk visibility, online (packets, `CTGPDelux010101`): mapped (agent_*.md), not implemented.
