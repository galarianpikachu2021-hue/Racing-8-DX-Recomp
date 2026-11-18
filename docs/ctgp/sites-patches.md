# CTGP patch group: FUN_000194c4 / FUN_000195fc / FUN_00019730 (78 sites) -> 4.0.1

Course count: the plugin reads uVar5 = courseHolder+8 = **0xbb (187 course indices; DB has 188 entries)** (emulator `boot.txt`: `H ...: ... 0xbb`). Written words: `mov r2,#uVar5+1=0xbc`, `cmp rX,#uVar5-1=0xba`, `movwgt rY,#0xbb`, `cmp rY,#0xbb`, `mov r1/r7,#0xbb` (0x4498fc), `cmp r0,#0xbb` (0x9436b8), `cmp r6,#0xbb` (0x44ae14). All decoded with capstone.

Key cross-build finding: 4.0.1 natively has **0x88 page/scene types** (3.0.3: 0x87). CTGP adds page type 0x87 in 3.0.3 (its Settings-style page; creator at plugin nro 0x276e0); in 4.0.1 id 0x87 is a game page, so the CTGP page must use **0x88** there, and page ids above 0x28 are shifted +1 in 4.0.1 (e.g. 0x73->0x74, 0x75->0x76 in 0x88a14).

## 0x12580 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x11cd4 in fn 0x11bec. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — match "strings"; same code shape

## 0x125c4 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r7, #0x7a` -> `cmp r7, #0xba`.
* **4.0.1 site**: 0x11d14 in fn 0x11bec. `cmp w23, #0x7a` -> `cmp w23, #0xba`. Regs: r7->w23.
* **Confidence**: high — match "strings"; same code shape

## 0x125c8 (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r4, #0x7b` -> `movwgt r4, #0xbb`.
* **4.0.1 site**: 0x11cf4 in fn 0x11bec. 64-bit uses `csinc w23, w24, w23, gt` with the constant hoisted before the loop: patch `mov w24, #0x7b` (0x11cf4) -> `mov w24, #0xbb`. Regs: r4->w23 (const in w24).
* **Confidence**: high — match "strings"; same code shape

## 0x125cc (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r4, #0x7b` -> `cmp r4, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x11d1c in fn 0x11bec. `cmp w23, #0x7b` -> `cmp w23, #0xbb`. Regs: r4->w23.
* **Confidence**: high — match "strings"; same code shape

## 0x11a028 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x105e90 in fn 0x105d18. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — caller 119cc0<->105b20 calls both D/C twins

## 0x11a068 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r6, #0x7a` -> `cmp r6, #0xba`.
* **4.0.1 site**: 0x105ed0 in fn 0x105d18. `cmp w24, #0x7a` -> `cmp w24, #0xba`. Regs: r6->w24.
* **Confidence**: high — caller 119cc0<->105b20 calls both D/C twins

## 0x11a06c (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r4, #0x7b` -> `movwgt r4, #0xbb`.
* **4.0.1 site**: 0x105eb0 in fn 0x105d18. 64-bit uses `csinc w24, w25, w24, gt` with the constant hoisted before the loop: patch `mov w25, #0x7b` (0x105eb0) -> `mov w25, #0xbb`. Regs: r4->w24 (const in w25).
* **Confidence**: high — caller 119cc0<->105b20 calls both D/C twins

## 0x11a070 (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r4, #0x7b` -> `cmp r4, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x105ed8 in fn 0x105d18. `cmp w24, #0x7b` -> `cmp w24, #0xbb`. Regs: r4->w24.
* **Confidence**: high — caller 119cc0<->105b20 calls both D/C twins

## 0x13e93c (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x129220 in fn 0x128e28. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — sole-caller-of match 13da40<->1285f4

## 0x13e980 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r4, #0x7a` -> `cmp r4, #0xba`.
* **4.0.1 site**: 0x129260 in fn 0x128e28. `cmp w20, #0x7a` -> `cmp w20, #0xba`. Regs: r4->w20.
* **Confidence**: high — sole-caller-of match 13da40<->1285f4

## 0x13e984 (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r5, #0x7b` -> `movwgt r5, #0xbb`.
* **4.0.1 site**: 0x129240 in fn 0x128e28. 64-bit uses `csinc w20, w28, w20, gt` with the constant hoisted before the loop: patch `mov w28, #0x7b` (0x129240) -> `mov w28, #0xbb`. Regs: r5->w20 (const in w28).
* **Confidence**: high — sole-caller-of match 13da40<->1285f4

## 0x13e988 (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r5, #0x7b` -> `cmp r5, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x129268 in fn 0x128e28. `cmp w20, #0x7b` -> `cmp w20, #0xbb`. Regs: r5->w20.
* **Confidence**: high — sole-caller-of match 13da40<->1285f4

## 0x1e3388 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x1c5648 in fn 0x1c5604. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — 13 twin loops in both builds in same order; neighbour-match deltas agree

## 0x1e33c8 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r7, #0x7a` -> `cmp r7, #0xba`.
* **4.0.1 site**: 0x1c5688 in fn 0x1c5604. `cmp w23, #0x7a` -> `cmp w23, #0xba`. Regs: r7->w23.
* **Confidence**: high — 13 twin loops in both builds in same order; neighbour-match deltas agree

## 0x1e33cc (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r6, #0x7b` -> `movwgt r6, #0xbb`.
* **4.0.1 site**: 0x1c5668 in fn 0x1c5604. 64-bit uses `csinc w23, w24, w23, gt` with the constant hoisted before the loop: patch `mov w24, #0x7b` (0x1c5668) -> `mov w24, #0xbb`. Regs: r6->w23 (const in w24).
* **Confidence**: high — 13 twin loops in both builds in same order; neighbour-match deltas agree

## 0x1e33d0 (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r6, #0x7b` -> `cmp r6, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x1c5690 in fn 0x1c5604. `cmp w23, #0x7b` -> `cmp w23, #0xbb`. Regs: r6->w23.
* **Confidence**: high — 13 twin loops in both builds in same order; neighbour-match deltas agree

## 0x1e5c18 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x1c7fa8 in fn 0x1c7f64. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — same; neighbours 1e58e8->1c7c64

## 0x1e5c58 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r7, #0x7a` -> `cmp r7, #0xba`.
* **4.0.1 site**: 0x1c7fe8 in fn 0x1c7f64. `cmp w23, #0x7a` -> `cmp w23, #0xba`. Regs: r7->w23.
* **Confidence**: high — same; neighbours 1e58e8->1c7c64

## 0x1e5c5c (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r6, #0x7b` -> `movwgt r6, #0xbb`.
* **4.0.1 site**: 0x1c7fc8 in fn 0x1c7f64. 64-bit uses `csinc w23, w24, w23, gt` with the constant hoisted before the loop: patch `mov w24, #0x7b` (0x1c7fc8) -> `mov w24, #0xbb`. Regs: r6->w23 (const in w24).
* **Confidence**: high — same; neighbours 1e58e8->1c7c64

## 0x1e5c60 (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r6, #0x7b` -> `cmp r6, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x1c7ff0 in fn 0x1c7f64. `cmp w23, #0x7b` -> `cmp w23, #0xbb`. Regs: r6->w23.
* **Confidence**: high — same; neighbours 1e58e8->1c7c64

## 0x20ff2c (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x1f11d8 in fn 0x1f1194. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — same; neighbour 210184->1f1430

## 0x20ff6c (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r7, #0x7a` -> `cmp r7, #0xba`.
* **4.0.1 site**: 0x1f1218 in fn 0x1f1194. `cmp w23, #0x7a` -> `cmp w23, #0xba`. Regs: r7->w23.
* **Confidence**: high — same; neighbour 210184->1f1430

## 0x20ff70 (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r6, #0x7b` -> `movwgt r6, #0xbb`.
* **4.0.1 site**: 0x1f11f8 in fn 0x1f1194. 64-bit uses `csinc w23, w24, w23, gt` with the constant hoisted before the loop: patch `mov w24, #0x7b` (0x1f11f8) -> `mov w24, #0xbb`. Regs: r6->w23 (const in w24).
* **Confidence**: high — same; neighbour 210184->1f1430

## 0x20ff74 (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r6, #0x7b` -> `cmp r6, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x1f1220 in fn 0x1f1194. `cmp w23, #0x7b` -> `cmp w23, #0xbb`. Regs: r6->w23.
* **Confidence**: high — same; neighbour 210184->1f1430

## 0x2b2390 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x28ff08 in fn 0x28fec4. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — same; neighbour 2b2e54->290998

## 0x2b23d0 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r7, #0x7a` -> `cmp r7, #0xba`.
* **4.0.1 site**: 0x28ff48 in fn 0x28fec4. `cmp w23, #0x7a` -> `cmp w23, #0xba`. Regs: r7->w23.
* **Confidence**: high — same; neighbour 2b2e54->290998

## 0x2b23d4 (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r6, #0x7b` -> `movwgt r6, #0xbb`.
* **4.0.1 site**: 0x28ff28 in fn 0x28fec4. 64-bit uses `csinc w23, w24, w23, gt` with the constant hoisted before the loop: patch `mov w24, #0x7b` (0x28ff28) -> `mov w24, #0xbb`. Regs: r6->w23 (const in w24).
* **Confidence**: high — same; neighbour 2b2e54->290998

## 0x2b23d8 (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r6, #0x7b` -> `cmp r6, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x28ff50 in fn 0x28fec4. `cmp w23, #0x7b` -> `cmp w23, #0xbb`. Regs: r6->w23.
* **Confidence**: high — same; neighbour 2b2e54->290998

## 0x37afe4 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0x3565c0 in fn 0x35657c. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — same; neighbours 37ae2c->3563e8

## 0x37b024 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r7, #0x7a` -> `cmp r7, #0xba`.
* **4.0.1 site**: 0x356600 in fn 0x35657c. `cmp w23, #0x7a` -> `cmp w23, #0xba`. Regs: r7->w23.
* **Confidence**: high — same; neighbours 37ae2c->3563e8

## 0x37b028 (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r6, #0x7b` -> `movwgt r6, #0xbb`.
* **4.0.1 site**: 0x3565e0 in fn 0x35657c. 64-bit uses `csinc w23, w24, w23, gt` with the constant hoisted before the loop: patch `mov w24, #0x7b` (0x3565e0) -> `mov w24, #0xbb`. Regs: r6->w23 (const in w24).
* **Confidence**: high — same; neighbours 37ae2c->3563e8

## 0x37b02c (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r6, #0x7b` -> `cmp r6, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0x356608 in fn 0x35657c. `cmp w23, #0x7b` -> `cmp w23, #0xbb`. Regs: r6->w23.
* **Confidence**: high — same; neighbours 37ae2c->3563e8

## 0xf2df0 (W32AT, FUN_000194c4)

* **What it does**: Debug-menu course list builder: creates an enum list with capacity 0x7c and adds items i=0..0x7b, each named by the course-name getter (3.0.3 0x1c490 / 4.0.1 0x1b4ac); loop: prev=-1; i=prev+1; add(i,name(i)); next = prev>0x7a ? 0x7b : i; until next==0x7b. Patch: list capacity `mov r2, #0x7c` -> `mov r2, #0xbc` (188 = DB entries).
* **4.0.1 site**: 0xe4344 in fn 0xe4308. `mov w2, #0x7c` (capacity arg of list init bl 0x784ce8) -> `mov w2, #0xbc`. Regs: r2->w2.
* **Confidence**: high — match "strings"

## 0xf2e34 (W32AT, FUN_000194c4)

* **What it does**: Loop limit test `cmp r5, #0x7a` -> `cmp r5, #0xba`.
* **4.0.1 site**: 0xe4388 in fn 0xe4308. `cmp w24, #0x7a` -> `cmp w24, #0xba`. Regs: r5->w24.
* **Confidence**: high — match "strings"

## 0xf2e38 (W32AT, FUN_000194c4)

* **What it does**: Clamp `movwgt r7, #0x7b` -> `movwgt r7, #0xbb`.
* **4.0.1 site**: 0xe4368 in fn 0xe4308. 64-bit uses `csinc w24, w25, w24, gt` with the constant hoisted before the loop: patch `mov w25, #0x7b` (0xe4368) -> `mov w25, #0xbb`. Regs: r7->w24 (const in w25).
* **Confidence**: high — match "strings"

## 0xf2e3c (W32AT, FUN_000194c4)

* **What it does**: Loop end `cmp r7, #0x7b` -> `cmp r7, #0xbb` (so items 0..0xbb are added; 188 items).
* **4.0.1 site**: 0xe4390 in fn 0xe4308. `cmp w24, #0x7b` -> `cmp w24, #0xbb`. Regs: r7->w24.
* **Confidence**: high — match "strings"

## 0x44ae78 (W32AT, FUN_000194c4)

* **What it does**: Function adds ids 0x10..0x3f then 0x43..0x7a (calls 0x44aea0(this,&id,1) per id, then -1). Patch `cmp r6, #0x7b` -> `cmp r6, #0xbb`: second loop runs 0x43..0xba, covering the CT course indices.
* **4.0.1 site**: 0x418ea0 in fn 0x418e3c. `cmp w20, #0x7b` -> `cmp w20, #0xbb` (loop calling 0x418ed0). Regs: r6->w20.
* **Confidence**: high — matched callee-of 4368f4; identical loops (0x10..0x40, 0x43..0x7b)

## 0x4d27fc (W32AT, FUN_000194c4)

* **What it does**: Cup-select screen input handler (3.0.3 0x4d2684): first page-change test (tst #0x1800, beq skip). Plugin writes `tst r5, #0x200` here, i.e. the test now checks button bit 0x200 instead of 0x1800/0x800/0x1000 (page/row change bound to another button; since the 0x800 test becomes the same 0x200 test, the 0x1000 branch is never reached).
* **4.0.1 site**: 0x496824 in fn 0x4966c8. `and w10, w20, #0x1800 (then cbz w10)` -> `and w10, w20, #0x200` (bit 9 = 0x200). Regs: r5->w20 (input trigger mask).
* **Confidence**: high — 4966c8 matched by strings; same field layout (+0x40f->+0x5a3, +0x414->+0x5a8), cmp #0xb/#0xc identical

## 0x4d287c (W32AT, FUN_000194c4)

* **What it does**: Cup-select screen input handler (3.0.3 0x4d2684): second (mirror) test (tst #0x1800, beq skip). Plugin writes `tst r5, #0x200` here, i.e. the test now checks button bit 0x200 instead of 0x1800/0x800/0x1000 (page/row change bound to another button; since the 0x800 test becomes the same 0x200 test, the 0x1000 branch is never reached).
* **4.0.1 site**: 0x496898 in fn 0x4966c8. `and w10, w20, #0x1800 (then cbz w10)` -> `and w10, w20, #0x200` (bit 9 = 0x200). Regs: r5->w20 (input trigger mask).
* **Confidence**: high — 4966c8 matched by strings; same field layout (+0x40f->+0x5a3, +0x414->+0x5a8), cmp #0xb/#0xc identical

## 0x4d2848 (W32AT, FUN_000194c4)

* **What it does**: Cup-select screen input handler (3.0.3 0x4d2684): `tst r5,#0x800; bne 0x4d2b24` (direction A). Plugin writes `tst r5, #0x200` here, i.e. the test now checks button bit 0x200 instead of 0x1800/0x800/0x1000 (page/row change bound to another button; since the 0x800 test becomes the same 0x200 test, the 0x1000 branch is never reached).
* **4.0.1 site**: 0x49686c in fn 0x4966c8. `tbnz w20, #0xb (bit 0x800)` -> `tbnz w20, #9` (bit 9 = 0x200). Regs: r5->w20 (input trigger mask).
* **Confidence**: high — 4966c8 matched by strings; same field layout (+0x40f->+0x5a3, +0x414->+0x5a8), cmp #0xb/#0xc identical

## 0x4d2854 (W32AT, FUN_000194c4)

* **What it does**: Cup-select screen input handler (3.0.3 0x4d2684): `tst r5,#0x1000; beq` (direction B). Plugin writes `tst r5, #0x200` here, i.e. the test now checks button bit 0x200 instead of 0x1800/0x800/0x1000 (page/row change bound to another button; since the 0x800 test becomes the same 0x200 test, the 0x1000 branch is never reached).
* **4.0.1 site**: 0x496870 in fn 0x4966c8. `tbz w20, #0xc (bit 0x1000)` -> `tbz w20, #9` (bit 9 = 0x200). Regs: r5->w20 (input trigger mask).
* **Confidence**: high — 4966c8 matched by strings; same field layout (+0x40f->+0x5a3, +0x414->+0x5a8), cmp #0xb/#0xc identical

## 0x4d28c8 (W32AT, FUN_000194c4)

* **What it does**: Cup-select screen input handler (3.0.3 0x4d2684): mirror of 0x4d2848 (strb 0 to +0x40f path). Plugin writes `tst r5, #0x200` here, i.e. the test now checks button bit 0x200 instead of 0x1800/0x800/0x1000 (page/row change bound to another button; since the 0x800 test becomes the same 0x200 test, the 0x1000 branch is never reached).
* **4.0.1 site**: 0x4968dc in fn 0x4966c8. `tbnz w20, #0xb` -> `tbnz w20, #9` (bit 9 = 0x200). Regs: r5->w20 (input trigger mask).
* **Confidence**: high — 4966c8 matched by strings; same field layout (+0x40f->+0x5a3, +0x414->+0x5a8), cmp #0xb/#0xc identical

## 0x4d28d4 (W32AT, FUN_000194c4)

* **What it does**: Cup-select screen input handler (3.0.3 0x4d2684): mirror of 0x4d2854. Plugin writes `tst r5, #0x200` here, i.e. the test now checks button bit 0x200 instead of 0x1800/0x800/0x1000 (page/row change bound to another button; since the 0x800 test becomes the same 0x200 test, the 0x1000 branch is never reached).
* **4.0.1 site**: 0x4968e0 in fn 0x4966c8. `tbz w20, #0xc` -> `tbz w20, #9` (bit 9 = 0x200). Regs: r5->w20 (input trigger mask).
* **Confidence**: high — 4966c8 matched by strings; same field layout (+0x40f->+0x5a3, +0x414->+0x5a8), cmp #0xb/#0xc identical

## 0x4e1434 (W32AT, FUN_000194c4)

* **What it does**: Copies a 16-byte per-course record (table[row r2][col r1], row = clamp(courseDB? +0xc80 value,0x19), col<8) into this+0x1cf8. Original `ldr r1, [r0, r1, lsl #4]!` (load + writeback r0 = &entry). Plugin `ldr r1, [sp, #0x10]`: word 0 of the copied record becomes the raw current course index ([sp+0x10] = [raceinfo-like obj +0x64], unclamped, may be >=0x7b), and because r0 is no longer advanced the remaining fields (+4,+8,+0xc byte) come from column 0 of the row.
* **4.0.1 site**: 0x4a3a88 + 0x4a3a8c in fn 0x4a3940. 4.0.1 splits it: `add x8, x8, x9, lsl #4` at 0x4a3a88 (writeback equivalent) -> nop; `ldr w9, [x8]` at 0x4a3a8c -> `ldr w9, [sp, #0x18]` (course index stored there at 0x4a3a50 from w25). Destination is x22+0x2088. Regs: r0->x8 (row ptr), r1->x9 (col / loaded word), [sp+0x10]->[sp+0x18] (course idx = w25).
* **Confidence**: high — 4a3940 sole-caller match; same cmp #0x7b/#0x19/#8 clamps, [+0xc80]/[+0xc84]

## 0x449914 (W32AT, FUN_000194c4)

* **What it does**: Init of a 0x7c-element per-course array (call 0x44ccb4(this,0x7c,1,...)). Plugin: count argument `mov r1, #0x7c` -> `mov r1, #0xbb` (187; note: not 0xbc).
* **4.0.1 site**: 0x417934 in fn 0x417908. `mov w1, #0x7c` (arg to bl 0x40bd38) -> `mov w1, #0xbb`. Regs: r1->w1.
* **Confidence**: high — matched by strings; identical sequence

## 0x44991c (W32AT, FUN_000194c4)

* **What it does**: Same function: the size stored into this+0x40 for the pointer buffer (`str r7,[r4,#0x40]`). Plugin sets 0xbb. CAVEAT: the buffer itself is still allocated as 0x1f0 bytes (=0x7c*4; 4.0.1 `mov w0,#0x3e0`) and the fill loop still runs to 0x7c, so the plugin leaves a size>capacity array; the port should also grow the allocation (4.0.1 0x417964 `mov w0,#0x3e0` -> 0xbb*8=0x5d8) and the fill loop limit (0x4179cc `cmp x8,#0x7c`) or bound-check accesses.
* **4.0.1 site**: 0x417940 in fn 0x417908. `mov w20, #0x7c` (stored to [x19,#0x78]) -> `mov w20, #0xbb`. Regs: r7->w20.
* **Confidence**: high — same

## 0x436928 (W32AT, FUN_000194c4)

* **What it does**: Creates a resource/heap holder via 0x94e6a8(obj, mgr, 0xa, 0x96, 0, 0). Plugin raises the 4th argument (capacity) 0x96 -> 0x300.
* **4.0.1 site**: 0x405fa4 in fn 0x405f74. `mov w3, #0x96` (arg to bl 0x8b3c74) -> `mov w3, #0x300`. Regs: r3->w3.
* **Confidence**: high — matched by strings; same args 0xa/0x96

## 0x437144 (W32, FUN_000194c4)

* **What it does**: Same pattern 0x94e6a8(obj, mgr, 0x5a, 0x46): capacity 0x46 -> 0x150.
* **4.0.1 site**: 0x406794 in fn 0x406760. `mov w3, #0x46` (arg to bl 0x8b3c74) -> `mov w3, #0x150`. Regs: r3->w3.
* **Confidence**: high — matched by strings; same args 0x5a/0x46

## 0x94424c (W32AT, FUN_000194c4)

* **What it does**: Parses a course index from two hex characters of a string; if >= 0x7b it is reset to 0 (and r8 cleared). Plugin: `cmp r0, #0x7b` -> `cmp r0, #0xbb`, so parsed CT indices up to 0xba are accepted.
* **4.0.1 site**: 0x8a9864 in fn 0x8a91e4. `cmp w8, #0x7b` (followed by two csel ...lo) -> `cmp w8, #0xbb`. Regs: r0->w8.
* **Confidence**: high — sole-caller match; identical hex-digit code (0xa9/0xd0)

## 0x1c4a0 (W32AT, FUN_000194c4)

* **What it does**: Course-name getter: `cmp r4,#0x7b; bhi return NULL`. Plugin replaces the bhi with a nop, so indices > 0x7b are no longer rejected (needed by the 188-entry lists above; other hooks redirect the actual name lookup).
* **4.0.1 site**: 0x1b4cc in fn 0x1b4ac. 4.0.1 has the inverted branch: `cmp w19, #0x7b; b.ls 0x1b4d8; mov x0,xzr; b ret`. Patch 0x1b4cc `b.ls` -> unconditional `b 0x1b4d8`. Regs: r4->w19.
* **Confidence**: high — known getter pair (briefing)

## 0x51a6d8 (W32, FUN_000194c4)

* **What it does**: Title/mode-select menu builder (strings L_Single_00, L_Multi_00, L_Online_00, L_LocalCom_00): creates 4 buttons (types 0..3) and registers each with 0x956d54. This call registers button type 2 = L_Online_00; plugin nops it.
* **4.0.1 site**: 0x4d7d5c in fn 0x4d7ba4 (title menu builder; f401 lists it inside 0x4d7a90). `bl #0x8bc0c4` after the type-2 button -> nop. Regs: -.
* **Confidence**: high — xref of L_Online_00 string at 0x4d7d08 in same block; ctor arg w8=#2

## 0x51a694 (W32, FUN_000194c4)

* **What it does**: Allocation `bl operator new(0x2e8)` for button type 2 (Online) -> `mov r0, #0`: allocation fails, ctor skipped, pointer stored to this+0x128 is NULL, i.e. the Online button object is never created.
* **4.0.1 site**: 0x4d7d1c in fn 0x4d7ba4. `bl #0x5902d0` (alloc 0x418) -> `mov x0, xzr`; cbz skips ctor 0x437e24, and `str x21,[x19,#0x1e0]` stores NULL. Regs: r0->x0.
* **Confidence**: high — as above

## 0x51d910 (W32, FUN_000194c4)

* **What it does**: Menu select-state update (3.0.3 0x51d8b0): for buttons +0x120..+0x12c calls on(0x46bde8) for the selected index, off(0x46be04) for the others. This is the on() call for button 2 (Online, now NULL); plugin nops it.
* **4.0.1 site**: 0x4da6d0 in fn 0x4da65c. `bl #0x437e80` (on) for [x20,#0x1e0] -> nop. Regs: r0=[this+0x128]->x0=[x20+0x1e0].
* **Confidence**: high — unique fn loading +0x1d0/+0x1d8/+0x1e0/+0x1e8 with same branch structure

## 0x51d920 (W32, FUN_000194c4)

* **What it does**: Same function: the off() call for button 2; nopped.
* **4.0.1 site**: 0x4da6e4 in fn 0x4da65c. `bl #0x437e98` (off) for [x20,#0x1e0] -> nop. Regs: r0=[this+0x128]->x0.
* **Confidence**: high — same

## 0x42bf18 (FUNC, FUN_000195fc)

* **What it does**: Cursor navigation of an 11-slot grid (original: slots 0..3 a column, 4..10 a row; 0x20 up, 0x40 down, 0x80 left, 0x100 right). Replaced by plugin 0x161ac (new layout incl. the Settings slots): p0: up->6, down->1, else 0. p1: up->0, down->2. p2: up->1, down->9. p3..5: right->p+1 (5->10), left->p-1 (3->9), up->2, down->p+3. p6..8: right->p+1 (8->6), left->p-1 (6->8), up->p-3, down->0. p9: right->3, left->10, up->2, down->6. p10: right->9, left->5, up->2, down->8. Priority for p3..10: right, left, up, down; for p0..2 only up then down. Other p (>10): p3..5/6..8 formulas fall through (p>10 handled by the 'else' branch: right->p+1, left->p-1, up->p-3, down->0).
* **4.0.1 site**: 0x3fc4a8 in fn 0x3fc4a8. Whole function 0x3fc4a8 (and w8,w2,#0x20; and w9,w2,#0x40; cmp w1,#3 ... ) ends 0x3fc534; next fn 0x3fc538 = 3.0.3 0x42bfa4. Regs: r1->w1 (cursor idx), r2->w2 (input bits), ret r0->w0.
* **Confidence**: high — identical logic (consts 3,0xa,4, bits 5/6/7/8); neighbour 42bfa4<->3fc538 also identical

## 0x957954 (FUNC, FUN_000195fc)

* **What it does**: Page/scene factory `create(this, heap, type)`: type clamped `<0x87`, creator = this->creators[type] (+4), info = this->infos[type] (+0x220), builds it, registers via 0x957eb8. Plugin handler calls this->vtbl[2] = plugin 0x361a4 on its own replacement factory (see 0x94bacc): same logic for type < 0x87, and for type >= 0x87 uses CTGP's extra creator/info (stored at factory+0x43c+type*4 / +0x440+type*4, i.e. +0x658/+0x65c for type 0x87), then registers via 0x36284. IMPORTANT for 4.0.1: the game already has 0x88 page types (cmp w19,#0x88; table 0x880 bytes; page ids above 0x28 shifted +1, e.g. 0x73->0x74), so CTGP's page id 0x87 collides with a native 4.0.1 page: use 0x88 (first free id) in 4.0.1.
* **4.0.1 site**: 0x8bcba4 in fn 0x8bcba4. 4.0.1 factory layout: creators at +8+type*8, infos at +0x448+type*8, `cmp w19,#0x88` at 0x8bcbe4; replace the function so type == CTGP id (0x88) uses the CTGP creator/info. Regs: r0..r2 -> x0 (factory), x1 (heap flag), w2 (page type).
* **Confidence**: high (mapping) / see note — score match confirmed: called from 0x8bcba4 -> bl 0x8bd0c8 (=957eb8); ctor 0x8bcb78 clears 0x880

## 0x957eb8 (FUNC, FUN_000195fc)

* **What it does**: Registry setter: `buf = this+0x18; if (type < size(+0x14)) buf[type] = obj else buf[0] = obj`. Replaced by plugin 0x36284: if type > size store into the plugin's global extra array at [type-0x87], else this->buf[type] (note: type == size writes buf[size], plugin off-by-one).
* **4.0.1 site**: 0x8bd0c8 in fn 0x8bd0c8. 4.0.1: size w at [x0+0x28], buffer at [x0+0x30], entries 8 bytes. Whole-function replacement at 0x8bd0c8. Regs: r0->x0 (registry), r1->x1 (&type), r2->x2 (object).
* **Confidence**: high — called at end of 0x8bcba4 exactly like 957eb8 in 957954

## 0x957f3c (INLINE, FUN_000195fc)

* **What it does**: Registry getter + dynamic-cast (3.0.3 0x957ed4): r4 = buf[type] (clamped as setter), then if r4 && r4->vfunc(+8)(typeinfo) return r4 else 0. Combined with the 15 FUN_00019730 overwrites of 0x957ed4..0x957eec (save this/&type in r4/r5, nop the original lookup), this inline hook at the `cmp r4,#0` runs plugin 0x362bc(this,&type): if type > size -> plugin extra[type-0x87] else buf[type]; result in r4.
* **4.0.1 site**: 0x8bd0f4 (replace lookup 0x8bd0f4..0x8bd10c) in fn 0x8bd0e8. 4.0.1 does the lookup at 0x8bd0f4..0x8bd10c with x0/x1 still live; hook at 0x8bd0f4: x19 = (type > [x0+0x28]) ? ctgp_extra[type-CTGP_BASE] : [[x0+0x30]+type*8]; pc=0x8bd110. (Equivalent to plugin's reordering; the guard-init block in between does not use the result.) Regs: r0(this)->x0, r1(&type)->x1, r4(result)->x19.
* **Confidence**: high — callee-of 959ec0 match; same structure

## 0x94bacc (INLINE, FUN_000195fc)

* **What it does**: Factory singleton creation: `r0 = new(0x43c); r0 = ctor 0x957924(r0); [r5+0xc] = r0`. With the 0x94bac4/0x94bac8 nops, the plugin replaces `bl 0x957924` by 0x16c44: allocate 0x444 bytes and construct the plugin's factory class (vtable + zeroed 0x440 bytes, i.e. 0x87 creators, 0x87 infos, extra creator/info). Note the game's creator table is then empty-initialized just like the original ctor (registration fills it later).
* **4.0.1 site**: 0x8b1314 in fn 0x8b1164. 4.0.1: `mov w0,#0x888` 0x8b1308; `bl 0x5fa960` 0x8b130c; `mov x22,x0` 0x8b1310; `bl 0x8bcb78` (ctor) 0x8b1314; `str x22,[x20,#0x18]`. Hook 0x8b1314: put the (extended) factory in x22 (and x0). For a host recreation it is simpler to keep the game factory (let 0x8b1308..0x8b1314 run) and keep the CTGP extra creator/info in host state used by the 0x8bcba4/0x8bd0c8/0x8bd0e8 replacements. Regs: r0->x0 and x22 (new factory).
* **Confidence**: high — sole-caller match; alloc 0x888 = 8+0x880

## 0x4367c4 (INLINE, FUN_000195fc)

* **What it does**: End of the big page-type registration function (3.0.3 0x43503c, last entry type 0x62): after the last `bl 0x94e6e8` the plugin (0x16bf8) registers its own page: factory=0x957a00(); factory+0x658 = CTGP creator (plugin nro 0x276e0); factory+0x65c = r4; 0x94e6e8(r4, &0x87, 10). Then returns as the original epilogue.
* **4.0.1 site**: 0x405f04 in fn 0x404410 (page registration fn; f401 boundaries unreliable). 4.0.1 registration ends with type 0x63 at 0x405f00 `bl 0x8b3cb0` then epilogue 0x405f04. Hook 0x405f04 with x19 = r4; use page id 0x88 in 4.0.1 (0x87 is native). Regs: r4->x19 (page manager / registration ctx).
* **Confidence**: medium — same per-type pattern (cmp #0x88 clamp, +8/+0x448 tables, bl 0x8b3cb0 w2=10); last-entry position, function boundary not cross-checked

## 0x8fcfc (INLINE, FUN_000195fc)

* **What it does**: Page-change handler: stores the new page id ([r1]) into this+0x124. Plugin stub repeats the store and calls 0x11c90(r0): saves the page id in CTGP global state (+0x1c) for its own page logic.
* **4.0.1 site**: 0x88a70 in fn 0x88a14. `str w8, [x19, #0x228]` at 0x88a70 (followed by cmp w8,#0x74). NOTE ids above 0x28 are +1 in 4.0.1 (3.0.3 0x73/0x75 -> 0x74/0x76) — code consuming the saved id must translate. Regs: r0->w8, r4->x19.
* **Confidence**: high — sole-caller match; identical mask 0x1002401 logic

## 0x50ad70 (INLINE0, FUN_000195fc)

* **What it does**: Cup/course screen state: flags word (+0x140) `orr r0, r0, #0x600` then stored to +0x140 and +0x154 (accepted-buttons mask). Plugin stub ORs 0x418 (0x410|0x8) instead of 0x600.
* **4.0.1 site**: 0x4c9958 in fn 0x4c9888. `orr w8, w8, #0x600` (stored to [+0x210] and [+0x238]) -> w8 |= 0x418 (not a valid single AArch64 logical immediate: hook sets w8 and skips the instruction). Regs: r0->w8.
* **Confidence**: high — callee-of matches; same ldr/orr/str/str sequence

## 0x50ae04 (INLINE0, FUN_000195fc)

* **What it does**: Cup/course screen state: flags word (+0x140) `orr r0, r0, #0x600` then stored to +0x140 and +0x154 (accepted-buttons mask). Plugin stub ORs 0x418 (0x410|0x8) instead of 0x600.
* **4.0.1 site**: 0x4c9a04 in fn 0x4c99f0. `orr w8, w8, #0x600` (stored to [+0x210] and [+0x238]) -> w8 |= 0x418 (not a valid single AArch64 logical immediate: hook sets w8 and skips the instruction). Regs: r0->w8.
* **Confidence**: high — callee-of matches; same ldr/orr/str/str sequence

## 0x432df8 (W32, FUN_00019730)

* **What it does**: Getter `return *list_b84[idx_bbc]` (current value of a selection list). Plugin makes it always return 0.
* **4.0.1 site**: 0x4028c0 in fn 0x4028b4. `ldr w0, [x8]` -> `mov w0, wzr` (4.0.1 offsets +0xef0/+0xf4c). Regs: r0->w0.
* **Confidence**: high — callee-of match; twin getter 0x432e00<->0x4028c8 follows

## 0x47097c (W32, FUN_00019730)

* **What it does**: Menu/list page set-up (strings Decide/Loop/Loop_Icon/T_Name_00/P_Pict_00): stores 0x201 into this+0x88 (an input/flag mask). Plugin stores 0x19 instead.
* **4.0.1 site**: 0x43ca54 in fn 0x43c8b8. `mov w8, #0x201` (stored to [x19,#0xfc]) -> `mov w8, #0x19`. Regs: r0->w8.
* **Confidence**: high — string xrefs Loop_Icon/T_Name_00/P_Pict_00 in 0x43c8b8; ldr [..,#0x1b0] <-> #0x108 call 0x8af3c0 just before

## 0x50a53c (W32, FUN_00019730)

* **What it does**: Input check `tst r0, #0x200; beq skip` -> `tst r0, #0x18`: action now triggered by bits 0x18 instead of 0x200.
* **4.0.1 site**: 0x4c91c8 in fn 0x4c906c. `tbz w8, #9, #0x4c933c` -> needs `tst w8,#0x18; b.eq 0x4c933c` (two instructions: hook). Regs: r0->w8 (input mask [x20+0x30]).
* **Confidence**: high — strings match; preceding tbnz #0xa (=tst #0x400) identical

## 0x957988 (W32AT, FUN_00019730)

* **What it does**: Inside factory create (0x957954): type clamp `cmp r4, #0x87` -> `#0x88` (one extra page type). Dead code in practice because 0x957954 is replaced by the FUNC hook.
* **4.0.1 site**: 0x8bcbe4 in fn 0x8bcba4. `cmp w19, #0x88` -> `#0x89` if keeping it consistent (4.0.1 natively has 0x88 types); no-op if the function is replaced. Regs: r4->w19.
* **Confidence**: high — same function as FUNC 0x957954

## 0x94bac4 (W32AT, FUN_00019730)

* **What it does**: `movw r0, #0x43c` (factory alloc size) -> word 0 (andeq = effectively nop). Part of replacing the factory allocation (see INLINE 0x94bacc).
* **4.0.1 site**: 0x8b1308 in fn 0x8b1164. `mov w0, #0x888` -> nop (only if the plugin's allocation replacement is reproduced). Regs: -.
* **Confidence**: high — sole-caller match

## 0x94bac8 (W32AT, FUN_00019730)

* **What it does**: `bl 0x66ae14` (operator new) -> nop.
* **4.0.1 site**: 0x8b130c in fn 0x8b1164. `bl #0x5fa960` -> nop (and 0x8b1310 mov x22,x0 then takes the hook's x0). Regs: -.
* **Confidence**: high — same

## 0x957ed4 (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `push {r4, r5, lr}` (prologue also saves r5). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957f64 (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `pop {r4, r5, pc}` (matching epilogue). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957f6c (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `pop {r4, r5, pc}` (matching epilogue (NULL path)). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957ed8 (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `mov r4, r0` (keep this in r4 (was ldr r1,[r1])). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957edc (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `mov r5, r1` (keep &type in r5 (was ldr r2,[r0,#0x14])). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957ee0 (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `nop` (was ldr r0,[r0,#0x18]). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957ee4 (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `nop` (was cmp r2,r1). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957ee8 (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `nop` (was addhi r0,r0,r1,lsl#2). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure

## 0x957eec (W32AT, FUN_00019730)

* **What it does**: Rewrite of registry getter 0x957ed4: `nop` (was ldr r4,[r0]). Together the 9 words remove the original lookup and preserve this/&type for the inline hook at 0x957f3c, which performs the extended lookup.
* **4.0.1 site**: 0x8bd0e8 (covered by hook at 0x8bd0f4) in fn 0x8bd0e8. No separate 64-bit patch: implement as one hook at 0x8bd0f4 replacing the lookup 0x8bd0f4..0x8bd10c (x0/x1 are still live there) -> x19, continue 0x8bd110. Regs: this->x0, &type->x1, result->x19.
* **Confidence**: high — getter 0x8bd0e8 same structure
