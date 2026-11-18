# Group "online": FUN_000184a0 (22 sites) + FUN_000180ec (13 sites)

Installer chain: `FUN_00011dc0` (plugin init) -> `FUN_000184a0` -> calls `FUN_000180ec` (the 13 W32AT nops) and then `FUN_000181dc`,
which installs the 22 hooks. Compact table: `agent_online.tsv`.

## Plugin helpers used by this group (ghidra addresses)

| fn | meaning |
|---|---|
| `0x15694` **comm()** | `G[6]->vtbl[0x10]() == 0` (G = plugin global block, `FUN_00011be0`). True = CTGP "communication mode" active, i.e. talk to CTGP peers with the widened record fields. Every widened-id hook below does its CTGP path only when this is true, otherwise it repeats the original code exactly. |
| `0x156d8` **count()** | `G[3]->field_8` = course-DB entry count - 1 (set in `FUN_00011cb0`: `FUN_00019aac(db) - 1`) = **0xbb (187)** for the 188-entry DB = number of course *indices* (0..0xba). It replaces the game's 0x7b. |
| `0x156ec` **randCT()** | if comm(): `a = rand(); if (rand() % 3 != 0) return 0x7b + a % (count() - 0x7b);` (a random CT course index 0x7b..0xba, 2/3 of the time); else / otherwise `-1`. |
| `0x15748` **key(buf)** | `strcpy(buf, comm() ? "CTGPDelux010101" : "MarioKart8Delux")` (pointer vars at nro 0x83064 / 0x83060 -> strings at nro 0x76381 / 0x76371). |
| `0x15794` **attr(session)** | if comm(): game `main+0x9a2910(session, 0, 0x10000002)` (`FUN_000156a8` returns `G[1]->field_4`, which `FUN_00011de8` sets to 0x10000002). Called via the main-relative trampoline nro 0x2f40 (offset word 0x9a2910). |
| `0x382a0` (nro 0x282a0) | hand-written copy of the game sort `0x8b90ec` whose swap is `FUN_00015900`: identical to the game swap, plus (when comm()) swapping the u32 at record+0xc. |

## Record layouts (3.0.3 vs 4.0.1)

* **12-byte "player course" record** (vote / per-station course): `+0..+5` bytes, `+6` byte = course index (bits 0..6) | flag (bit 7),
  `+8` u16, `+0xa` u8, `+0xb` pad. The game copies records as 8 bytes + u16@8 + u8@0xa (3.0.3 0x8e49d8..; 4.0.1 0x84dac4..). CTGP (comm
  mode) stores the **full course index as a u32 at +8** (bit 31 = the flag), overwriting the u16/u8 there. Array stride 12 in both
  builds (`add r3,r3,r3,lsl#1; ...lsl#2` / `madd x8,x8,#0xc`); records reached through an object are at **obj+0x10** in both (3.0.3
  `add r1,r0,#0x16` = obj+0x10+6; 4.0.1 `add x8,x0,#0x10` then `[x8,#6]`). So CTGP's word is at **record+8 = obj+0x18** in both builds.
* **0x18-byte "player/station" record** (12 of them, sorted by `0x8b90ec`/`0x824078`): `+0` u64 id, `+8` u8, `+9` u8, `+0x10` byte =
  course index (bits 0..6) | flag (bit 7). `+0xa..+0xf` and `+0x11..+0x17` are padding (the game only ever writes u64@0, u16@8, u8@0x10;
  4.0.1 init at 0x8241a8 zeroes just those). CTGP stores the **full course index as a u32 at +0xc** (bit 31 = the flag). Layout
  identical in 4.0.1.

Plain int course values on the stack/out-params stay 32-bit in 4.0.1 (`str w8,[x20]` etc.).

---

## 0x8dfac0 (W32AT, FUN_000180ec)
**What it does**: in `0x8df868` (send/refresh of the per-player course bytes) the game writes the 7-bit course `r4 & 0x7f` into byte +6 of
each record keeping bit 7 (`and r2,r4,#0x7f; ldrb r0,[r1]; and r0,#0x80; orr r0,r2; strb r0,[r1]`). This overwrites `and r2,r4,#0x7f`
with `nop`; the INLINE hook at 0x8dfac8 does that work itself (see there).
**4.0.1 site**: 0x8491ac (`bfxil w9,w27,#0,#7`) in `0x848f54`; no separate `and`. Handled entirely by the 0x8dfac8 hook (0x8491a8).
r4->w27. **Confidence** high (same loop, same vfunc call 0xc0/0x180, `+0x10`/`+6`).

## 0x8dfacc (W32AT)
**What**: nops `and r0,r0,#0x80` (part of the read-modify-write above). **4.0.1**: part of `bfxil` at 0x8491ac; covered by the 0x8491a8 hook.
high.

## 0x8dfad0 (W32AT)
**What**: nops `orr r0,r0,r2`. With these three nops `strb r0,[r1]` (0x8dfad4, not patched) stores whatever the 0x8dfac8 stub left in r0.
**4.0.1**: covered by the 0x8491a8 hook (skip to 0x8491b4). high.

## 0x8dfb98 (W32AT)
**What**: nops `ands r0,r0,#0x7f` after `ldrb r0,[r0,#6]` (check that every player's course byte is non-zero / identical); the
INLINE 0x8dfb94 stub produces the masked value and flags. **4.0.1**: 0x849248 `and w8,w8,#0x7f`; covered by the 0x849244 hook. high.

## 0x8ea69c (W32AT)
**What**: nops `and r0,r0,#0x7f` (read of the local record's course into `*out`); stub 0x8ea698 does the masking. **4.0.1**: 0x853140,
covered by the 0x85313c hook. high.

## 0x8e4a28 (W32AT)
**What**: nops `and sl,r0,#0x7f` (pick of the course from the player whose type == 2); stub 0x8e4a24 sets sl. **4.0.1**: 0x84db0c
`and w22,w8,#0x7f`, covered by the 0x84db08 hook. high.

## 0x8e4afc (W32AT)
**What**: `and r2,sl,#0x7f` -> `mov r2,sl`: the value written back into every record (see 0x8e4b14) is the full (unmasked) course.
Unconditional (also when comm is off; harmless since sl is then already 7-bit).
**4.0.1 site**: 0x84dbec `and w10,w22,#0x7f` in `0x84d948` -> `mov w10,w22`. r2->w10, sl->w22. high.

## 0x8e4b18 / 0x8e4b20 / 0x8e4b24 / 0x8e4b28 (W32AT x4)
**What**: nops `and r6,r7,#0x7f`, `andne r7,#0x80`, `orrne r7,r2`, `strbne r7,[r3]` (keep `cmp r6,sl` at 0x8e4b1c). The stub at 0x8e4b14
does load/mask/compare-prep/store itself. **4.0.1**: 0x84dc04 / 0x84dc10 / 0x84dc14 / 0x84dc18 in `0x84d948`; all covered by the hook at
0x84dc00 (resume at 0x84dc1c). r7->w13, r6->w14, r2->w10, r3->x12. high.

## 0x8e1788 (W32AT)
**What**: nops `and sl,r0,#0x7f` in `0x8e1634` ("all players agree on the course" check); stub 0x8e1784 sets sl. **4.0.1**: 0x84abcc
`and w26,w8,#0x7f`, covered by the 0x84abc8 hook. high.

## 0x8e1850 (W32AT)
**What**: nops `ands r1,r0,#0x7f` (same function, second loop); stub 0x8e184c sets r1 + flags. **4.0.1**: 0x84acb4, covered by the
0x84acb0 hook. high.

## 0x8e29a8 (INLINE0, FUN_000184a0)
**What**: in `0x8e2964` (is this course selectable for this player: course in 0x10..0x7a, ownership/DLC checks; called only by the
course-from-votes pickers 0x8e1058 / 0x8e1358) the site is `cmp r0,#0` after `bl 0x8ec09c` (a bool). The stub runs `cmp r0, count()-1`
(r0 restored) instead. Since r0 is 0/1 and count()-1 = 0xba, Z is always clear, so the following `bne 0x8e2a10` is **always taken**
(the ownership loop 0x8e29b0..0x8e2a0c is skipped). The stub (0x28120) is the plugin's "`cmp rX,#0x7a` -> `cmp rX,count-1`" stub, and
the real `cmp r0,#0x7a` is exactly 0x18 bytes earlier (0x8e2990); most likely the plugin's address is off by 6 instructions. Either way
it has no visible effect: both callers' checks of this function's result are nop'd (0x8e11b8, 0x8e1468), so the function's result is
ignored in CTGP.
**4.0.1 site**: 0x84bc38 `tbnz w0,#0,#0x84bc8c` (= `cmp r0,#0; bne`) in `0x84bbec`; exact effect = make it an unconditional `b 0x84bc8c`.
(Intended site would be 0x84bc24 `cmp w8,#0x7a` -> `cmp w8,#0xba`.) r0->w0. **Recommendation**: skip (dead after the caller nops) or
apply the exact effect. medium (mapping high; intent uncertain).

## 0x8e2a5c (INLINE0)
**What**: same function, site `beq 0x8e2a7c` (null check of the `0x87a110` result). The same stub replaces the branch with a `cmp`, so
the null check is **removed** (falls through to `ldr r1,[r0+0xe0+...]`). Again 0x18 after the real `cmp r0,#0x7a` at 0x8e2a44, so
probably a mis-addressed hook. Result of the function is ignored anyway.
**4.0.1 site**: 0x84bcc0 `cbz x0,#0x84bce0` -> exact effect `nop` (would crash on null). Intended site: 0x84bcb4 `cmp w8,#0x7a`.
r0->x0. **Recommendation**: skip. medium.

## 0x52c6e8 (INLINE0)
**What**: in the 3.0.3 function at **0x52bc84** (not in f303.tsv; sites.txt's "fn 0x52c6e8" is the raw address) the game picks a
course with `0x42b748(..., &[sp+0x1a8], ...)` (the "Random" pick) and submits it with `0x3c0fa4(&course, 1)` (-> 0x8e0418 -> 0x8b8a1c,
the vote setter). At `mov r1,#1` (r0 = picked course index) the stub does `mov r1,#1; r0' = randCT(); if r0' == -1 keep r0`. So in comm
mode a random pick becomes a random CT track 2/3 of the time.
**4.0.1 site**: function **0x4e6e34** (f401/m303_401 have it as part of 0x4e6b84; that match is wrong for this site). 0x4e75a4
`ldr w8,[sp,#0x628]`; hook at **0x4e75a8** (`mov x0,sp`, before `str w8,[sp]`): `w8 = randCT() != -1 ? randCT : w8`. r0->w8.
**Confidence** high (same callee pair 0x42b748->0x3fbf74, 0x3c0fa4->0x39875c, then `strb #0,[obj+0x418/0x5ac]`).

## 0x8de8ec (INLINE)
**What**: in `0x8de044` (big online/local race-setup state update), after `0x8b8f88` produced the course index of a player in
`[sp+0xc0]`->`[sp+0xb8]`: `if (c < 0x10 || c >= 0x7b) invalid_flag = 1`. Stub: `cmp r3, count()` (0xbb), so CT indices count as valid.
**4.0.1 site**: 0x84820c `cmp w10,#0x7b` in `0x847aa4`; r3->w10; action `cmp w10, 0xbb`. high (auto-map agrees).

## 0x8b90bc (INLINE)
**What**: in `0x8b8f88` (look up a player's record by id; returns course index to `*fp`, flag to `*sl`), local-play branch: course is
kept when `0x4b <= c < 0x7b`, otherwise replaced by a random one. Stub: `cmp r0, count()`.
**4.0.1 site**: 0x824048 `cmp w8,#0x7b` in `0x823f08`; r0->w8; `cmp w8, 0xbb`. high.

## 0x8b9010 (INLINE)
**What**: same function: `*out_course = rec[+0x10] & 0x7f`. Stub: `v = rec[+0x10] & 0x7f; if comm() v = rec.u32[+0xc] & 0x7fffffff;
*fp = v`.
**4.0.1 site**: 0x823fa4 `str w8,[x20]` in `0x823f08` (x22 = rec+0x10, x20 = out). Action: `[x20] = comm ? [x22-4] & 0x7fffffff :
[x22] & 0x7f`. fp->x20, r5->x22-0x10. high.

## 0x8b8e68 (INLINE)
**What**: in `0x8b8a1c` (set/insert a player's course into the 12-entry 0x18-byte array: `rec->course = (*course & 0x7f) | flag<<7`).
Stub: does that store, then if comm(): `rec.u32[+0xc] = (*course & 0x7fffffff) | flag<<31` and
`rec.byte[+0x10] = (*course ? 0x10 : 0) | flag<<7` (placeholder course 0x10 in the 7-bit field).
**4.0.1 site**: 0x823df8 `strb w9,[x8,#0x10]` in `0x823a44` (x8 = rec, x21 = course ptr (arg x2), w19 = flag (arg w3)). r0->x8,
fp->x21, sl->w19. high.

## 0x8b9074 (INLINE0)
**What**: `0x8b8f88`, battle/random branch: `rec->course = *fp | 0x80` (just-picked random course + flag). Stub: does that, then if
comm(): `rec.u32[+0xc] = *fp | 0x80000000`; `rec.u32[+0x10] = (*fp ? 0x10 : 0) | 0x80` (a **32-bit** store: bytes +0x11..+0x13 zeroed;
they are padding).
**4.0.1 site**: 0x824004 `strb w8,[x22]` in `0x823f08` (x22 = rec+0x10, x20 = fp). Action: original; if comm `[x22-4] = [x20] |
0x80000000; u32 [x22] = ([x20] ? 0x10 : 0) | 0x80`. high.

## 0x8b9004 (INLINE0)
**What**: `0x8b8f88`: `*flag_out = rec[+0x10] >> 7`. Stub: same, then if comm(): `*flag_out = rec.u32[+0xc] >> 31`.
**4.0.1 site**: 0x823f98 `strb w8,[x19]` (x19 = flag out). Action: `strb [x19] = comm ? [x22-4]>>31 : [x22]>>7`. sl->x19,
r5->x22-0x10. high.

## 0x8e4a24 (INLINE)
**What**: `0x8e4844` (host-side: copy every player's 12-byte record into the session table, take the course of the type-2 player into
sl): `sl = rec[+6] & 0x7f` (rec = return of vfunc 0x88). Stub: `sl = comm ? rec.u32[+8] & 0x7fffffff : rec[+6] & 0x7f`.
**4.0.1 site**: 0x84db08 `ldrb w8,[x0,#6]` + 0x84db0c `and w22` in `0x84d948` (vfunc 0x110). Action: `w22 = comm ? [x0+8]&0x7fffffff
: [x0+6]&0x7f`, resume 0x84db10. r0->x0, sl->w22. high.

## 0x8e4b14 (INLINE)
**What**: same function, final loop: for each player record in the table, if its 7-bit course != sl, rewrite it to `sl` (keeping bit 7).
Stub with comm(): `w = rec.u32[+8]; r6 = w & 0x7fffffff; if (r6 != sl) { rec.byte[+6] = (u8)(w + 0x80); rec.u32[+8] = r2 (= sl) }`
(the byte gets the low byte of the old word plus 0x80, exactly as coded). Without comm: original code.
**4.0.1 site**: 0x84dc00 `ldrb w13,[x12,#6]!` in `0x84d948`; hook covers 0x84dc00..0x84dc18, resume 0x84dc1c. r3->x12 (record
base before writeback), r7->w13, r6->w14, sl->w22, r2->w10 (= w22 after the 0x84dbec patch). high.

## 0x8dfac8 (INLINE)
**What**: `0x8df868`: write course `r4` into each player's record byte +6 (record = vfunc_0xc0(...) + 0x10, r1 = &byte+6). Stub with
comm(): `obj.u32[+0x18] (= rec.u32[+8]) = r4 & 0x7fffffff`, byte +6 left unchanged (r0 = old byte, re-stored by 0x8dfad4). Without
comm: `(old & 0x80) | (r4 & 0x7f)` as original.
**4.0.1 site**: 0x8491a8 `ldrb w9,[x8,#6]` in `0x848f54` (x8 = obj+0x10 or 0, w27 = course). Action: `if comm: [x8+8] = w27 &
0x7fffffff; skip to 0x8491b4` else original. Do not clobber NZCV (set by `cmp x24,x23` at 0x8491a4, used by `b.ne` at 0x8491b4).
r0->x0, r1->x8+6, r4->w27. high.

## 0x8dfb94 (INLINE)
**What**: `0x8df868`, check loop: `c = rec[+6] & 0x7f; if c == 0 -> fail`; all players must have the same course (low byte compare).
Stub: `r0 = comm ? rec.u32[+8] & 0x7fffffff : rec[+6] & 0x7f` with flags.
**4.0.1 site**: 0x849244 `ldrb w8,[x0,#6]` (+ 0x849248 and; 0x84924c `cbz w8`). Action: `w8 = comm ? [x0+8]&0x7fffffff : [x0+6]&0x7f`;
resume 0x84924c. high.

## 0x8ea698 (INLINE)
**What**: `0x8ea62c` (get the local player's course): `*out = rec[+6] & 0x7f`. Stub: `r0 = comm ? obj.u32[+0x18] & 0x7fffffff :
byte & 0x7f`.
**4.0.1 site**: 0x85313c `ldrb w8,[x8,#6]` in `0x8530cc` (x8 = obj+0x10, x19 = out). Action: `w8 = comm ? [x8+8]&0x7fffffff :
[x8+6]&0x7f`; resume 0x853144. r0(obj)->x8-0x10. high.

## 0x8e1784 (INLINE)
**What**: `0x8e1634` (all players' course == the session's stored course `[slot+0x1b0]` / 4.0.1 `[slot+0x1d0]`): `sl = rec[+6] & 0x7f`.
Stub: `sl = comm ? obj.u32[+0x18] & 0x7fffffff : byte & 0x7f`.
**4.0.1 site**: 0x84abc8 `ldrb w8,[x8,#6]` (x8 = obj+0x10; x0 is already overwritten by `mov x0,x19`). Action: `w26 = comm ?
[x8+8]&0x7fffffff : [x8+6]&0x7f`; resume 0x84abd0. sl->w26. high.

## 0x8e184c (INLINE)
**What**: `0x8e1634` second loop, like 0x8dfb94: `r1 = rec[+6] & 0x7f` (flags), 0 -> fail, all equal.
**4.0.1 site**: 0x84acb0 `ldrb w8,[x0,#6]`; action `w8 = comm ? [x0+8]&0x7fffffff : [x0+6]&0x7f`; resume 0x84acb8 (`cbz w8`).
r0->x0, r1->w8. high (auto-map listed 2 candidates; this is the second loop).

## 0x8d6460 (INLINE0)
**What**: `0x8d6394` = local-play (LAN/LDN) session setup B (network-lib object r4 selected by mode 0/1/2; calls setters 0x9a29e8,
0x9a2500, 0x9a25d4, 0x9a2844(r4,0), 0x9a277c(r4,1), ...). At `mov r0,r4` (before 0x9a277c) the stub calls `attr(r4)`: if comm,
`0x9a2910(r4, 0, 0x10000002)`, a 2-argument setter of the same family (index 0 = value; most likely a session attribute), so CTGP sessions
carry a different attribute value from vanilla ones.
**4.0.1 site**: 0x840874 (`mov w1,#1`, right after `bl 0x902ea8` = 0x9a2844) in `0x84079c`; action: if comm call **0x902f70**(x19, 0,
0x10000002). 3.0.3 0x9a2910 -> 4.0.1 0x902f70 by setter order (0x9a277c->0x902de0, 0x9a2844->0x902ea8, 0x9a2910->0x902f70,
0x9a29e8->0x903040) and inner callees (0x9bdfd8->0x920d24, 0xa09718->0x9614d4). r4->x19. medium (fn match is score 1.70 but the call
sequence matches 1:1; 0x902f70 identification by order).

## 0x8d62d8 (INLINE0): the 'MarioKart8Delux' -> 'CTGPDelux010101' key
**What**: `0x8d623c` = local-play session setup A. It copies a 32-byte constant `"MarioKart8Delux\0..."` (3.0.3 0xde77c0) to
`[sp+0x80]`, then after `0x9a2bc4(r4, sp, 0x80)` calls `0x9a2ccc(r4, sp+0x80, 0x20)` (sets the 32-byte communication key / passphrase
of the local session). At `mov r0,r4` (0x8d62d8) the stub calls `key(sp+0x80)`: `strcpy(buf, comm ? "CTGPDelux010101" :
"MarioKart8Delux")`. In comm mode CTGP consoles therefore only find each other (separate local-play pool). This is the only place the
game uses the string (one occurrence in each main).
**4.0.1 site**: 0x8406fc (`mov x0,x19`, after `bl 0x9029d4` and `mov x1,sp`) in `0x840660`; key buffer = **x29-0x30** (= sp+0x80;
filled from 0xf734d4 "MarioKart8Delux" + 16 zero bytes), consumed by `bl 0x903344(x19, x29-0x30, 0x20)` at 0x840730. Action:
`strcpy(x29-0x30, comm ? "CTGPDelux010101" : "MarioKart8Delux")` (both 15 chars + NUL; rest stays zero). Equivalent alternative: patch
the buffer at any point before 0x840730. high.

## 0x8e2374 (INLINE0)
**What**: `0x8e2198` (merge the received 12-entry player list (sl, 0x18-byte records) into the local one via `0x8b8a1c`, then sort with
`0x8b90ec`): before the call, `tmp = src[+0x10] & 0x7f; flag = bit7` are passed as `0x8b8a1c(dst, rec, &tmp, flag)`. At `movwlt r3,#1`
the stub repeats it and, if comm(), sets `tmp (= *r2) = src.u32[+0xc] & 0x7fffffff` (r8 = source record).
**4.0.1 site**: 0x84b7bc (`mov x0,x20`, common block of both paths before `bl 0x823a44` at 0x84b7c4) in `0x84b5c4`. Action: if comm
`[x2] = [x23+0xc] & 0x7fffffff` (x23 = x22 + idx*0x18 = source record; x2 = sp or sp+8). r2->x2, r8->x23. high.

## 0x8b90ec (FUNC)
**What**: game function = sort of the 12 x 0x18-byte player records (12 passes; `if (a[i].id < a[j].id || (== and (b8,b9) less))
swap(a[i], a[j])`); its swap exchanges u64@0, u16@8 and byte@0x10 (only). Replacement (nro 0x282a0) = same algorithm and comparison;
its swap `FUN_00015900` does the same field exchange and, if comm(), also exchanges **u32 @ +0xc** (the widened course).
**4.0.1 site**: function entry **0x824078** (x0 = array; callers 0x8487ec, 0x84b7d8). Reimplement as above (the 4.0.1 original copies
u64@8 into its temp but restores only u16@8, i.e. same semantics as 3.0.3). r0->x0. high.

## 0x8e11b8 (W32)
**What**: in `0x8e1058` (choose the race course from the players' votes: collect players whose voted course passes `0x8e2964`, pick one
at random) `bne` after `cmp r0,#1` on `0x8e2964`'s result -> `nop`: every vote is accepted (the check rejects course indices > 0x7a,
i.e. all CT tracks).
**4.0.1 site**: 0x84a680 `tbz w0,#0,#0x84a6d0` (after `bl 0x84bbec` at 0x84a67c) in `0x84a498` -> `nop`. high.

## 0x8e1468 (W32)
**What**: in `0x8e1358` (other vote pick path) `beq` after `cmp r0,#0` on `0x8e2964`'s result -> `nop` (same reason).
**4.0.1 site**: 0x84a89c `tbz w0,#0,#0x84a8e0` (after `bl 0x84bbec` at 0x84a898) in `0x84a780` -> `nop`. high.

---

### Notes for the implementer
* All widened-field reads/writes use `record+8` (12-byte records, also `obj+0x18`) or `record+0xc` (0x18-byte records), same offsets
  in 4.0.1.
* count() should be the imported course-DB size - 1 (0xbb for the 188-entry DB); randCT uses `[0x7b, count)`.
* comm() is a CTGP setting; the port needs an equivalent switch (off = vanilla behaviour + vanilla key, which keeps compatibility
  with unmodified consoles; on = CTGP key + attribute 0x10000002 + widened ids).
