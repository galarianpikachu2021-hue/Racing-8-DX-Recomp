# Group "ui": cup screen, menus/Settings, HUD (music switch, speedometer, race timer, honk), file redirects, speed multiplier

Scope: all sites whose installer is FUN_000153d8 (9), FUN_00015538 (6), FUN_0001562c (1), FUN_000184d4 (11) + FUN_00018634 (1),
FUN_00018644 (4), FUN_000186ec (11), FUN_00036a7c (3) = 46 sites. Machine-readable table: `agent_ui.tsv`.
Helpers I wrote (scratch only): `$S/ui/` (disassembler with string/adrp annotation, 4.0.1 adrp xref index, plugin decompile
viewer that resolves the plugin's game-call thunks to 3.0.3 addresses).

## Plugin state used by these handlers (needed to reimplement)

`S` = plugin global (`FUN_11be0`, nro 0x86778), fields (32-bit words):

| field | meaning |
|---|---|
| S[0] (+0x00) | music redirect manager: +4 = CTGP stream root dir index, +8 = per-course stream dir index (`content://Course/<folder>/Resource/Stream/`), uses a 300-byte static path buffer |
| S[2] (+0x08) | cup state (0x28 B): byte0 = CTGP cups active, byte1 = flag (line count adjust 7), **byte2 = "vanilla/no paging" (hides N_ChangeIcon_01)**, +0x20 = current extra-cup line (page), +0x24 = line count; +0x08/+0x14 cup maps |
| S[3] (+0x0c) | course DB holder `{db, cupdb?, maxIndex(=0xbb)}`; `db.entry(id)` via FUN_19bf4 (id = course index + 1) |
| S[4] (+0x10) | plugin "modules" table; module[0]+0xc = honk-visibility module enabled (see HUD) |
| S[5] (+0x14) | race music state: +0 = lap variant of last pBGM request (0 `_N`, 1 `_LAP2`, 2 `_F`, -1), +4 = bool custom stream found, **+8 = float SpeedMultiplier** |
| S[6] (+0x18) | 5 CTGP settings (game option-row objects, value = vfunc+0x10, 0/1/(2)): [0] Communication Mode (CTGPDX Players / Vanilla Players), [1] Honk Visibility (Enabled/Disabled), [2] Default Race BGM (CTGPDX OST / Author's Pick / None; 3 choices), [3] Race Timer, [4] Speedometer (On/Off). Getter used by plugin: 0 -> 0, 1 -> 1, anything else -> -1 |
| S[7] (+0x1c) | last menu page id (written by group 0x95fc hook at 3.0.3 0x8fcfc = `[obj+0x124]` in the page-change function) |

Course DB entry fields used: +0x00 folder name, +0x08/+0x0c file digests, +0x28 BGM course index, +0x2c selected music set
(0 = CTGPDX OST, 1 = Author's Pick -> `_B` streams), +0x32 user-chose-set flag, +0x38 music variant list (size = number of sets).

**Menu page ids differ in 4.0.1**: ids <= 0x3a are equal; from 0x3e on 4.0.1 = 3.0.3 + 1 (verified on the 0x85/0x86-entry page
jump tables: 3.0.3 0x80d44, 4.0.1 0xf21e94; and on the page lists at 0x8f980/0x88728: 0x74->0x75, 0x3e->0x3f, 0x60->0x61,
0x81->0x82, 0x83->0x84, max 0x84->0x85). CTGP's new Settings page id 0x87 (3.0.3) should be **0x88** in 4.0.1 (any id > 0x85 not
used by the game works; it must be the same constant at all 5 page sites). 0x21, 0x22, 0x23 are unchanged.

Game function equivalents used by the handlers (3.0.3 -> 4.0.1): race singleton 0x906fb4->0x86f7bc; find pane 0x947fe4->0x8ad8e8;
set pane visible 0x948588->0x8ade74; set pane text 0x94830c->0x8adc54; get message 0x948270->0x8adb88; set pane texture
0x948100->0x8ada44; anim play 0x947aa0->0x8ad300; anim set frame 0x947b38->0x8ad3c4; anim is-playing 0x947cd8->0x8ad57c;
UI part init from pane 0x956d54->0x8bc0c4; part base ctor 0x468d20->0x434d90; operator new(size,align) 0x5fa4ec->0x5902d0;
CupIcon ctor 0x46f7fc->0x43b924 (size 0x2d4 -> 0x3e0); CupIcon::set(icon,&cup,&state) 0x46fa64->0x43bb68; cup-icon setup of a
cup page 0x4d0668->0x494b44; page transition set 0x94c928->0x8b209c; page start transition 0x955220->0x8ba5fc; load texture
0x9511bc->0x8b63f8; race-system getter 0x87a17c->0x7e7434; local player index 0x8e971c->0x8521f4; pad 0x959f1c->0x8bf158;
int->text 0x95a238->0x8bf59c; byaml iter init 0x95b894->0x8c0b10; byaml try-get-float 0x95bd04->0x8c0fb0; course map root
0x8908f0->0x7fc894; state change 0x956b38->0x8bbee0.

---

## FUN_000153d8 (file redirects, course checks, BGM index, title voice)

### 0x82640 (INLINE, FUN_000153d8)
**What it does**: title screen (fn 0x815a0) picks a random title voice clip from a 6-entry table `{SE_SYS_VO_TITLE_SINGLE/MARIO/LUIGI/KOOPA/ROSETTA/LINK, wait}`
and loads the wait time `[tbl+idx*8+4]` into r1 (passed to 0x84900). Plugin: r1 = FUN_16c74(r4) = own table
`{0x90,0x78,0x78,0x6c,0x64,0xa0}` (only entry 0 changes 0x78 -> 0x90: CTGP's replaced "single" voice is longer); idx>=6 -> 0x78.
**4.0.1**: 0x7c340 `ldr w21,[x10,#8]` in fn 0x7b1cc (table 0x11b5218, 16-byte entries, idx clamped in w8). r4->w8, r1->w21.
High (identical table contents found).

### 0x669480 (INLINE)
**What it does**: fn 0x669450 opens a file (`nn::fs::OpenFile(obj+4, path=r1, mode)`). Plugin: `r1 = FUN_16c98(r1) ?: r1`.
FUN_16c98: idx=[race+0x1a0]; only if 0x10 <= idx <= 0xbb: FUN_11018 sets S[5].lap = 0/1/2/-1 for paths containing pBGM and
`_N.bfstm`/`_LAP2.bfstm`/`_F.bfstm`; then FUN_116b0(S[0], path, idx+1): returns 0 unless path contains `Audio/Resource/Stream/`.
Non-pBGM stream: if S[0]+8 (course stream dir) is set, strip `content://Audio/Resource/Stream/` and return that dir + name if
the file exists there. pBGM stream: suffix = first of `_GOAL/_RESULT/_START_GRID/_REPLAY/_N/_LAP2/_F.bfstm` in path (else
`.bfstm`); try `"pBGM_" + db[id].folder + ("_B" if db[id]+0x2c==1) + suffix` in the CTGP stream root (S[0]+4), then without
`_B`; S[5].found = result!=0; returns the full path (static 300-byte buffer) or 0.
**4.0.1**: 0x5f8ea0 (`add x0,x0,#8`, before `bl 0xb53d30`) in fn 0x5f8e70; r1->x1. High.

### 0x7f6464 (INLINE)
**What it does**: fn 0x7f6418 (model/resource set-up) tests bit 1 of byte 0x13 of a resource header (`res+[res+0xc]`); if set
(and other flags) it stores the resource pointer into `[arg1+0x44]`. Plugin clears bit 1 in the loaded byte, so that path is
never taken. **4.0.1**: 0x768b80 `ldrb w9,[x9,#0x13]` (then `tbz w9,#1`), fn 0x768b24; r1->w9; action `w9 &= ~2`.
High (map401 + identical structure).

### 0x437208 (INLINE) and 0x426da0 (INLINE)
**What it does**: two points where a race scene loads (0x437114: `UI/cmn/race.sarc` / `UI/%s/race.sarc`; 0x426d00:
`UI3D/Theater/Theater.bfres`). Plugin (registers untouched): `if (!FUN_17df4()) crash` (stub 0x27ee0 zeroes all registers and
jumps to 0 = deliberate anti-tamper crash), then FUN_16da8().
FUN_17df4: for course id ([race+0x1a0]+1) < 0x10 -> ok; else reads `content://Course/<folder>/course_kcl.szs` and
`.../course_muunt.byaml`, hashes them and compares with entry digests +0x08 / +0x0c; missing digests or mismatch -> fail.
FUN_16da8: for course id in 0x10..max: frees S[0]+8, clears the path buffer, S[5].lap=-1, then
S[0]+8 = directory index of `content://Course/<folder>/Resource/Stream/` (per-course stream overrides used by 0x669480).
**4.0.1**: 0x406850 (`mov x0,x19`) in 0x406760; 0x3f8098 (`add x0,sp,#0x28`, before `bl 0x8806c4`) in 0x3f8004. No register
mapping needed. Port: do the stream-dir set-up; the hash check/crash should be dropped (or turned into a warning). High.

### 0xa4a4c (INLINE)
**What it does**: race BGM controller init (fn 0xa4a2c, strings BGM_COURSE_IN_Cnsw18 / FinalLapFanfare) stores the course index
into `[this+0xc4]` (BGM course index). Plugin: r0 = FUN_16404([race+0x1a0]+1) = db[id].field 0x28 (BGM course index; id >= count -> entry 0).
**4.0.1**: fn 0x9c044 (not in f401; found via xref of `BGM_COURSE_IN_Cnsw18` at 0x9c454), site 0x9c078 `ldr w8,[x0,#0x1a0]`;
result -> w8 (stored to [x19+0x108]). High.

### 0x8fa28 (INLINE)
**What it does**: page-change sound handler (fn 0x8f980) skips a BGM call (0x7e0b4/0x847a4, 30 frames) for pages 0x20,0x74,0x21,
0x22,0x3e,0x60,0x81,0x83. Plugin adds 0x87 (CTGP Settings page) to that list.
**4.0.1**: 0x887ec `cmp w8,#0x84` in 0x88728 (list there is 0x20,0x75,0x21,0x22,0x3f,0x61,0x82,0x84). Action: if w8==0x84 or
w8==0x88 -> pc=0x88804. High.

### 0x80d24 (INLINE)
**What it does**: menu BGM/page switch on `[x+0x12c]` page id (jump table 0..0x84). Plugin: r0 = FUN_161a0(r0): 0x87 -> 0x22
(Settings page behaves like page 0x22). **4.0.1**: 0x7abc8 `cmp w8,#0x85` in 0x7aa48; w8 = (w8==0x88)?0x22:w8. High.

### 0xa5ce4 (INLINE)
**What it does**: race BGM start (fn 0xa5ccc) loads BGM course index `[this+0xc4]`, skips if <0, else plays.
Plugin: r1 = FUN_16de8() ? 0 : [r4+0xc4]; FUN_16de8 = (setting[2] "Default Race BGM" value is "None" (getter -1)).
**4.0.1**: 0x9d170 `ldr w1,[x19,#0x108]` in 0x9d14c; r4->x19, r1->w1. High.

## FUN_00015538 (cup select screen)

### 0x4d08e0 (INLINE)
**What it does**: inside the cup-icon set-up of a cup page (fn 0x4d0668(page, bool)): after the game sets its 18 icons
(`[page+0x390+4*i]`, 4.0.1 `[page+0x4c0+8*i]`), plugin FUN_16fb8(page=r6): if the page is in the CTGP map, for i=0..7:
`CupIcon::set(extraIcon[i], &cupId(0x18+i), &state(5))`.
**4.0.1**: 0x494d60 (`ldr x21,[x19,#0x540]`, start of icon 16 set-up; any point in the function works) in 0x494b44; r6->x19.
Medium (exact position free; structure identical: icons 0x15/0x16/0x17 at +0x538/+0x540/+0x548).

### 0x52b300, 0x4d3c20, 0x4d37b0 (INLINE) - createCtgpCupIcons
**What it does**: end of three cup-page constructors (0x52b224 = page with Random button `L_CourseSlctBtnRandom_00`, which first
calls 0x4d3ae8; 0x4d3ae8 and 0x4d35f0 = cup/course page layout set-up). Plugin FUN_1745c(page=r4, layout=[r4+0x3ec]).
FUN_1745c: increments a global call counter and **does nothing on the 3rd call ever** (counter never reset); otherwise for
names `L_CupIcon_18`..`L_CupIcon_25`: icon = new CupIcon (0x2d4 B, ctor(icon,1)), `partInit(page, icon+0x18, layout, name, 0)`,
clear bit31 of icon+0x48 (4.0.1 +0x88), push icon; pane = findPane(layout, name), push pane. If the page is not yet in the global
`unordered_map<page, {vector<CupIcon*>, vector<Pane*>, Pane* N_ChangeIcon_01}>`, insert it; else the new vectors are just emptied.
**4.0.1**: 0x4e6588 in fn 0x4e64a8 (found via xref `L_CourseSlctBtnRandom_00`; layout [x19+0x578]); 0x497ae0 (epilogue of
0x4979b8); 0x4976bc (epilogue of 0x497524). r4->x19, [r4+0x3ec]->[x19+0x578]. CupIcon in 4.0.1: alloc 0x3e0 via 0x5902d0(size,4),
ctor 0x43b924(obj,1), part at +0x30. High.

### 0x4d1624 (INLINE)
**What it does**: entry of fn 0x4d1618 (cup page open/refresh). Plugin FUN_16ef4(page=r0): calls cup-icon set-up (0x4d0668(page,0))
again, then if page is in the map and S[2].byte2 != 0 hides N_ChangeIcon_01. **4.0.1**: 0x4958e4 `mov x19,x0` in 0x4958c8; r0->x0. High.

### 0x4c9350 (INLINE)
**What it does**: fn 0x4c92f4 = cup-icon decided handler (event [r1+4] = icon index < 0xb, [r1+0x14]==0; reads icon cup id
`[icon+0x2cc]`). Plugin FUN_16f6c(page=r4): `if ([page+0xa4]-1) >= 2 -> 0x4d0668(page, 0)` (re-set icons unless state 1/2).
**4.0.1**: 0x48e1ec `mov x0,x19` in 0x48e190 (icons at x19+0xd60, cup id +0x3d0; matched via callee 0x489610 + same event
layout). r4->x19. The page state field 0xa4 is probably 0x120 in 4.0.1 (0x9c -> 0x118 seen in 0x4e64a8); verify. Medium.

## FUN_0001562c

### 0x6953e4 (INLINE)
**What it does**: fn 0x6953dc = archive/resource load (r0 archive holder, r2 = SafeString*). Plugin: `alt = FUN_17cc8(r0, [r2+4])`,
if alt: `[r2+4] = alt`. FUN_17cc8: only for course id 0x10..max; if the path contains `MapObj/` -> alt = `Course/<folder>/` + path
(FUN_150f8), and keep it only if `holder->vfunc+0x58(exists)` (4.0.1 vtable +0xb0) says it exists; `RaceCommon/Coin` -> FUN_15014
always returns 0 (disabled). So course-local MapObj overrides.
**4.0.1**: 0x6231c0 (`mov w20,w3`) in 0x6231a4; r0->x0, r2->x2, string pointer [r2+4] -> [x2+8]. High.

## FUN_000184d4 + FUN_00018634 (speed multiplier, lap/timer)

Speed multiplier sites: all are `v *= 1/|v|` normalisations in kart physics; the plugin makes them `v *= SpeedMultiplier/|v|`
(`FUN_1598c` = S[5]+8). 4.0.1 multiplier value is the same float.

| 3.0.3 | fn 3.0.3 -> 4.0.1 | 4.0.1 site | regs | conf |
|---|---|---|---|---|
| 0x278e8 vdiv s0,s4,s0 (vector on stack sp+0x10..0x18) | 0x27764 -> 0x26044 (same shape: 2x sqrt/div, call 0x8c3d98, stack vector) | 0x261c4 `fdiv s2,s1,s0` | s0->s0, result s0->s2 | medium |
| 0x226f8 (field 0x2dc..0x2e4) | 0x22618 -> 0x214c8 | 0x215a4 `fdiv s1,s1,s0` (field 0x378..) | s4->s1 | medium |
| 0x227f8 (stores 0x2bc..0x2c4) | 0x22618 -> 0x214c8 | 0x21784 `fdiv s0,s1,s0` (stores 0x354..0x35c) | s0->s0 | medium |
| 0x22898 (stack) | 0x22618 -> 0x214c8 | 0x21824 `fdiv s1,s1,s0` | s2->s1 | medium |
| 0x354f8 (field 0x2a0..0x2a8) | 0x3535c -> 0x332f8 (via caller 0x33d98->0x31db8 callee order) | 0x334b0 (field 0x348/0x344/..) | s4->s1 | medium |
| 0x3a558 (field 0x2a0) | 0x3a334 -> 0x37ff8 (3 sqrt/div blocks, same order) | 0x38244 | s4->s1 | high |

### 0x19d648, 0x19d690 (INLINE)
**What it does**: fn 0x19d3ec computes two kart values stored to `[r4+0x3dc]` (and 0x3e0); plugin stores `s0*SpeedMultiplier`
to 0x3dc and leaves s0 scaled (so 0x3e0 at the second site also gets the scaled value).
**4.0.1**: fn 0x182c14, field 0x3dc -> 0x42c, 0x3e0 -> 0x430. Site 0x19d648 -> 0x182e2c `str s1,[x19,#0x42c]` (value in s1);
site 0x19d690 (merge point of two paths) -> two 4.0.1 stores: 0x182e40 and 0x182e7c (`str s0,[x19,#0x42c]`). Action: multiply
the stored register by the multiplier before the store. Medium.

### 0x11f830 (INLINE) - where the multiplier comes from
End of fn 0x11f590 (course/race load). Plugin FUN_15938: S[5]+8 = 1.0; `root = 0x8908f0()` (race system -> [obj+0x120]+0x12c ->+4,
the course map BYAML root, probably course_muunt.byaml); if root: BYAML iter init + try-get-float key **"SpeedMultiplier"** ->
S[5]+8. So per-course multiplier from the course file, default 1.0, no clamping/range check.
**4.0.1**: 0x10b95c (`add sp,sp,#0x220`, after `blr x8`) in 0x10b6c8. 4.0.1 calls: 0x7fc894, 0x8c0b10, 0x8c0fb0. High.

### 0x123de0 (INLINE) + 0x123dc8 (W32AT nop)
**What it does**: fn 0x123cd4 (race HUD; r5 = lap count from rule [+0x28]) sets an animation frame (0x8f042c(obj,3,s0,s1)) from the
lap count only for rule-mode byte `[[[raceObj4]+0x11c]+0x30]+0x34` == 3 (laps-1) or 7 (laps+2). The W32AT nop removes the
`bne` so every mode reaches the call; the INLINE sets the frame value to FUN_159a0(laps): mode 2 -> 2, 3 -> laps-1, 4 -> laps+8,
5 -> laps+11, 6 -> laps+15, 7 and others -> laps+2.
**4.0.1**: fn 0x10fa04 (verified: same structure, mode byte at [[[x8+0x218]+0x58]+0x64], laps in w20). nop 0x10faec
(`b.ne 0x10fb10`); hook 0x10fb00 (merge point, `fmov s0,wzr`): s1 = (float)f(w20). High.

### 0x3c45bc (INLINE)
**What it does**: fn 0x3c4588 builds a time value object (limits 9:59.999) initialised to 300000 ms (5:00.000) - race time limit.
Plugin: r1 = (course index [race+0x1a0] > 0x7a, i.e. a CT track) ? 0 : 300000. **4.0.1**: 0x39b978 (after `movk w1,#0x93e0`,
before `bl 0x87875c`) in 0x39b950; r1->w1. High.

## FUN_00018644 (in-race HUD: speedometer, music switch, race timer)

### 0x55fba0 (INLINE)
**What it does**: HUD part init (fn 0x55fb18) picks its player's rule entry with the part's own index `ldrsb [this+0x79]`.
Plugin replaces it with the local player index (FUN_15c04 = max(0, 0x8e971c())), so the part (timer part when shown in non-TT
modes) follows the local player. **4.0.1**: 0x515370 `ldrsb w9,[x19,#0xdd]` in 0x5152f8 (found by pattern; identical code);
w9 = max(0, 0x8521f4()). High.

### 0x57ee90 (INLINE)
**What it does**: race HUD builder (fn 0x57ea28): `cmp r0(race mode),#2; bne` -> creates the `L_Timer_00` part only in mode 2
(time trial). Plugin: FUN_15a2c(this, [this+0x228]+0x18) creates two plugin HUD parts, `L_Sus_00` (0x88 B, music-set switch,
class FUN_3746c) and `L_MusSwch_00` (0x78 B, now-playing banner, class FUN_36d1c), then sets flags with `cmp FUN_15c44(),#1`:
FUN_15c44 = if solo (race mode [race+8] not 3/6 and 0x94769c()+0xcc == 1) and setting[3] Race Timer value == 0 -> 1, else
(mode == 2). So the timer is shown in any solo race when Race Timer is on.
**4.0.1**: 0x531d94 `cmp w8,#2` in 0x53198c; this->x19, [this+0x228]->[x19+0x320], layout part = [x19+0x320]+0x30; action:
create parts; pc = wanted ? 0x531d9c : 0x531f58. High. (The two parts are plugin classes; they must be reimplemented.)

### 0x55ceb0 (INLINE)
**What it does**: coin-counter part init (fn 0x55cdd0, `T_Coin_00`/`T_Molec_00`). Plugin FUN_15d58(layout=r5): g_TSOM =
findPane(layout,"T_SOM_00"); if not solo (FUN_15c08) or setting[4] Speedometer value == 1 (Off): hide `W_SOMBase_00`.
**4.0.1**: 0x5129b0 (`str x0,[x19,#0xc0]`) in 0x5128e0 (xref of T_Coin_00); r5->x20 (=this+0x30). High.

### 0x55d32c (INLINE)
**What it does**: entry of the lap-counter part update (fn 0x55d324). Plugin FUN_15c94: T_SOM_00 text =
`int(abs(kart.speed) * 10)` where kart = [[[[[raceSys+0xf0]+0x12c]+0x68]+localIdx*4]+4]+0x14, speed float at kart+0x330 (32-bit
offsets; 64-bit kart path/field must be re-derived). **4.0.1**: 0x5132e8 (`mov x19,x0`) in 0x5132d0 (strings SE_RC_LAP/LAP2/
SE_SYS_RACE, calls 0x512cf0 = match of 0x55d208). Medium (function match by strings; kart field not mapped).

## FUN_000186ec (title menu: CTGP Settings page)

### 0x51d950 (INLINE)
Plugin handler FUN_15e34 is **empty** (no-op). 4.0.1 equivalent: entry of 0x4da71c (0x4da72c); no hook needed. Medium.

### 0x4d93ac (INLINE)
Entry of fn 0x4d93a4 (cup/course page enter). Plugin FUN_15e38: if S[7]==0x21: if setting[0] Communication Mode == 0
(CTGPDX Players) {S[2].byte0=1, byte2=0} else {byte0=0, byte2=1}; if S[7]==0x23: byte0=1, byte2=0; other pages: unchanged.
**4.0.1**: 0x49c89c in 0x49c88c (page ids 0x21/0x23 unchanged). High.

### 0x51c874 (INLINE)
**What it does**: title-menu button handler (fn 0x51c004 -> 4.0.1 0x4d92e8, found in the f401 gap after 0x4d9224; identical
button ids). Cursor-on events: switch(id-0x14) sets r6 (=-1/0..) and r7 (title picture kind 0..0xc); default path = `mov r6,#0`.
Plugin: r6 = (id-0x14==0x57)?-1:0, r7 = (==0x57)?0xd:0 (button 0x6b = CTGP Settings -> picture kind 0xd).
**4.0.1**: 0x4d988c `mov w21,w22` (default); r0->w8, r6->w21, r7->w22. High.

### 0x51acfc (INLINE0)
**What it does**: title-menu page build (fn 0x51a4e8 -> 4.0.1 0x4d7ba4) where button 0x6a is created. Plugin FUN_15e88(page=sl):
new part (0x2c8 B, base ctor 0x468d20) with id [+0x24]=0x6b, partInit(page, part+0x18, [page+0x31c], "L_SubBtn_07"), text pane
`T_Btn_00` = message 813000 ("Settings").
**4.0.1**: 0x4d8274 `mov w8,#0x6a` in 0x4d7ba4; sl->x19, layout [sl+0x31c] -> x20 = [x19+0x448]; in 4.0.1 part size/base
offsets: part +0x30, id +0x48 (as the 0x6a button). High.

### 0x51c1f0 (INLINE0)
**What it does**: "decide" event of the same handler: switch(id-0x64) for 0..6. Plugin FUN_16110: if id-0x64==7 (button 0x6b):
`transition(G).set(&page 0x87, &0, -1)` (0x94c928), `[this+0xe0]=0x19`, `0x955220(this, G)`, and return from the game function
(stub does the epilogue). G = plugin-made transition object (0x28 B, game ctor 0x94c0e8).
**4.0.1**: 0x4d93dc `sub w9,w8,#0x64` in 0x4d92e8; if w8==0x6b: 0x8b209c(G,&0x88,&0,-1); [x19+0x184]=0x19; 0x8ba5fc(x19,G);
pc=0x4da640 (epilogue). G: allocate a transition like `this+0x638` (game's own transition objects). High.

### 0x49d9ec (INLINE0)
**What it does**: option-list builder fn 0x49aebc (11 KB switch on list kind `[r5]` 0..7); kind > 7 goes to a stub that sets
r1=r8=r6=0 and jumps to the common tail. Plugin FUN_15f50(list=r4, kind=[r5]): if kind==8 (CTGP Settings): add header 810000
("Network Settings") + rows setting[0], setting[1]; header 810001 ("Race Settings") + rows setting[3], setting[4], setting[2];
then r1=[r4+0x234] (headers), r0=[r4+0x238] (rows) reloaded.
**4.0.1**: 0x466604 (default target of `cmp w9,#7; b.hi`) in 0x463d50; list r4->x19 (offsets 0x234->0x2f0, 0x238->0x2f4),
kind ptr r5->x21; set w9=[x19+0x2f0], w8=[x19+0x2f4], w21=w22=0, pc=0x4664f8. Header/row add helpers (FUN_362f4/3636c) write
list+0x224/0x228/0x22c/0x230/0x234/0x244 and call 0x488fe8 (4.0.1 0x452f88); 64-bit offsets must be re-derived. Medium.

### 0x51b5b0 (INLINE)
`cmp r0,#0x83` (page id from 0x95517c) -> also accept 0x87. **4.0.1**: 0x4d89d0 `cmp w8,#0x84` in 0x4d8350; Z=1 also for 0x88. High.

### 0x509480 (INLINE)
Title picture loader (fn 0x508d7c, loads TitleBG*.bntx). Plugin FUN_16084(res=fp, layout=[sb+0x120]): tex = load
`TitlePicSetting_00^l.bntx`; set it on pane `P_Setting_00`. **4.0.1**: 0x4c82d8 in 0x4c7c68; fp->x20, layout [x19+0x1d0]. High.

### 0x507044 (INLINE)
Title page pane lookup (fn 0x506a94). Plugin FUN_15fe0([r4+0x120]): g_NSetting = findPane(layout, "N_Setting_00").
**4.0.1**: 0x4c6048 `ldr x0,[x19,#0x1d0]` (before `P_TiteleBG_01`) in 0x4c5b30. High.

### 0x5073c0 / 0x50768c (INLINE)
Title picture switch fn 0x5073a4(this, int* kind, bool): plugin hides g_NSetting at entry (FUN_16034), and after the kind switch
FUN_16058(r0): shows g_NSetting if r0==0xd (r0 equals the kind only on the kind>0xc path). **4.0.1**: fn 0x4c6360; entry hook
0x4c637c; post-switch hook 0x4c6618 (`tbz w21`); use `[x19]` (kind) == 0xd. High / medium.

## FUN_00036a7c (honk visibility, online)

Flag `honkMod` = FUN_36aec = byte +0xc of plugin module S[4]->module[0]; the module is enabled/disabled through a settings row
setter (FUN_379c0: value 1 -> enable, else disable); by the texts this is setting [1] "Honk Visibility" (meaning of 0/1 not proven).

### 0x3bc964 (INLINE0)
Kart status-flags builder (fn 0x3bc???, 4.0.1 0x394110) ORs bits into `[r4+0x34]` (0x2000, 0x4000, 0x10000...). Plugin, if honkMod:
clear bit 0x80000, then set it if `[[r5+0x24]+0x178] & 0x10000` (the kart's honk bit, set at 0x18cd6c's code from byte +0x123);
then the original `ldr r8,[r5,#0x24]`. **4.0.1**: 0x3942ec `ldr x22,[x20,#0x48]`; r4->x19 (+0x34 same), r5->x20,
[r5+0x24]->[x20+0x48], +0x178 -> +0x1cc (from 0x173f38). Medium.

### 0x18cd6c (INLINE0)
Kart update (fn 0x18cd6c area, 4.0.1 0x173e30): `r0 = byte[r4+0x123]` (honk input) then bit 0x10000 of [r4+0x178] = r0!=0.
Plugin: if honkMod && byte[r4+0x93] && ([[r4+0x10]+0x13f0] & 0x80000) then r0 = 1 (a remote kart whose received flags have
bit 0x80000 honks visibly). **4.0.1**: 0x173f34 `ldrb w8,[x19,#0x177]`; +0x123->+0x177, +0x178->+0x1cc; +0x93, +0x10, +0x13f0 not
mapped (64-bit offsets differ). Low (offsets).

### 0x1a87f8 (W32 nop)
Packet writer (fn 0x1a853c): `[pkt] = [r5+0x34] & 0x1ffff` -> nop keeps bit 0x80000 in the sent word. **4.0.1**: 0x18d5dc
`and w8,w8,#0x1ffff` -> nop, in 0x18d2d8. High.

---

## Feature notes

**Cup screen paging.** The game page already has 18 icons (`[page+0x390+4i]`, 4.0.1 `[page+0x4c0+8i]`, cups 0..23 paged by the
game's own line change). CTGP adds 8 icons `L_CupIcon_18..25` (+ uses pane `N_ChangeIcon_01` and the page's own anims
`CupIconLineChange_00`, `InAll_RL/LR`, `OutAll_RL/LR`, `LBtnDown`, `RBtnDown`, set up by plugin FUN_36400) created per page in a
global map (FUN_1745c) and always set to cups 0x18..0x1f with state 5 (FUN_16fb8). State: S[2]+0x20 = current extra line,
+0x24 = line count, byte1 adjusts the max (count-7 instead of count-1), byte2 = no CTGP cups (vanilla online) -> paging hidden.
Paging (group 0x8ccc, FUN_17190 at 0x4d268c + FUN_36900/368cc/367b8): in page state [page+0xa4]==0 and not animating, cursor
index [page+0x414] < 12, pad bits 0x1000 (R) -> next line (wrap to 0), 0x800 (L) -> previous; plays the line-change anim on
`[[page+0x98]+0x2c]` (direction by button), state -> 6; in state 6 when the anim ended: re-run the cup icon set-up
(0x4d0668(page,1)) with the new line, state -> 0. Extra panes are hidden in state 4 and shown in state 2. The cup id ->
course mapping for 24..39 comes from the cup database (other group). Online `Vanilla Players` turns CTGP cups off (FUN_15e38).

**CTGP Settings page.** New title-menu button `L_SubBtn_07` (id 0x6b, text 813000) opens new page 0x87 (4.0.1: 0x88), which the
game treats like page 0x22 (BGM switch) and 0x83/0x81 (lists). Its option list is list kind 8 with two headers and five rows
(game option-row class, ctor 0x4806c8, value via vfunc+0x10): Communication Mode, Honk Visibility, Race Timer, Speedometer,
Default Race BGM. Title picture kind 0xd shows `N_Setting_00` with texture `TitlePicSetting_00^l.bntx` on `P_Setting_00`.
Where values persist (save file) is outside these sites.

**Music.** Stream requests (`Audio/Resource/Stream/pBGM_*`) are redirected to `pBGM_<CT folder>[_B]{_N,_LAP2,_F,_START_GRID,
_GOAL,_RESULT,_REPLAY}.bfstm` in the CTGP stream folder (`_B` = Author's Pick set = entry+0x2c==1), other streams to
`content://Course/<folder>/Resource/Stream/`; MapObj files to `Course/<folder>/MapObj/...`. BGM settings index = entry+0x28.
In race, `L_Sus_00` lets the player switch the music set (entry+0x2c 0<->1, only if the course has >= 2 sets; text
800000 "CTGPDX OST"/800001 "Author's Pick" on T_Coin_00, SE_SYS_SOUNDTEST_CHANGE_1/2; pad bits 0xc0 of
[0x947744()+0x144]+4); default set from setting [2]. `L_MusSwch_00` shows song name/author/prefix/sub name and lap variant
(610000+lap) from the course music variant record (labels 6xxxxx), shown when race mode != 6, <= 2 local players and
Default Race BGM != None. "None" makes race BGM start with BGM course index 0.

**Speed multiplier.** Float per course from the course map BYAML key `SpeedMultiplier` (default 1.0 every load, no range check),
applied by scaling 6 normalised kart vectors (`v *= m/|v|`) and two kart limit values (0x3dc/0x3e0; 4.0.1 0x42c/0x430).

**Race timer / lap HUD.** Race Timer setting shows the time-trial `L_Timer_00` part in solo races (one local player, mode != 3/6);
the timer part uses the local player's index; lap-counter frame formula per rule mode (see 0x123de0); CT tracks get time value 0
instead of 5:00.000 at 0x3c45bc.

**Speedometer.** `T_SOM_00` in the coin layout (`W_SOMBase_00` hidden unless solo and Speedometer On) shows |speed|*10 every
lap-part update.

**Honk visibility.** With the module on, the own kart's honk bit (0x10000 of kart+0x178) is copied into status flag 0x80000,
which is no longer masked out of the online packet; received karts with that flag get their honk byte forced to 1.

**Title voice.** Wait time of title voice 0 changed 120 -> 144 frames.

## Unmapped / caveats
* No site left without a 4.0.1 address. Low-confidence parts are offsets inside handlers: 0x18cd6c (+0x93, +0x10, +0x13f0),
  speedometer kart path (+0x330), option-list helper offsets (+0x224..+0x244), cup page state +0xa4.
* FUN_1745c ignores its 3rd call ever; the port should decide whether to keep that (probably to skip one page instance).
* The course-file hash check (crash on mismatch) at 0x437208 / 0x426da0 should not be ported as a crash.
