# CTGP course-id group (installer FUN_00018ccc): 3.0.3 -> 4.0.1

Counts: {'high': 65, 'medium': 9}. All 74 sites mapped. Convention: when the 3.0.3 hooked instruction is a load, new_site_401 is the equivalent 4.0.1 load; the hook runs that load (or skips it when the action fully replaces the value) and then applies `action` to the destination register. FUNC = the function entry.

Helper names used below (plugin C functions, Ghidra addresses; courseDB indexed by course **id** = index+1):

* `db.base(id)` = FUN_16618: courseDB[id].0x14 base course index (0 if id > count).
* `db.baseBCP(id)` = FUN_16654: like base, but if id > 0x7b and base > 0x4a (a BCP track) -> 0x10.
* `db.name(id)` 0x18 / `db.platform(id)` 0x1c / `db.author(id)` 0x24 (-1 if out of range); `db.folder(id)` +0x00 string, `db.picName(id)` +0x04 string (out of range -> entry 0); `db.cup(id)` (int8)+0x33 (0 if oob); `db.flag34(id)` (int8)+0x34.
* `cupIconName(id)` = FUN_163b4: cupDB[db.cup(id)].iconName (cup+8). `remapCup(c)` = FUN_166bc (CTGP cup page/shuffle remap). `cupSlotCourse(cup,slot)` = FUN_1382c (course index of a CTGP cup slot, with remap). `cupSel(cup,slot)` = FUN_165a8. `slotCourse/slotPlatform/slotName(id)` = FUN_1650c/164a8/16444 (resolve the track's own cup slot, or its alt list if cup==-2, and return course index / +0x1c / +0x18).
* 3.0.3 race-info getter 0x906fb4 -> 4.0.1 0x86f7bc; the second getter 0x906fc4 -> 0x86f7d4. Game UI helpers: FindPane 0x947fe4->0x8ad8e8, GetMessage 0x948270->0x8adb88, SetText 0x94830c->0x8adc54, SetVisible 0x948588->0x8ade74.

## 0x4e73e8 (INLINE, 3.0.3 fn 0x4e6d94, installer FUN_00018ccc)

* **What it does**: Menu scene data field +0x1cf8 (4.0.1 +0x2088) = selected course index is loaded into r0, stored to the stack and passed by pointer to 0x8d4988 (4.0.1 0x83ede0). Hook replaces the next instruction `mov r1,r5` (4.0.1 `mov w1,w21`, same position): stub 0x28c90 clamps r0: idx>=0x7b -> 0x10, then does mov r1,r5.
* **4.0.1 site**: 0x4aa554 in fn 0x4a9f40; regs: r0->w8 (value just loaded by ldr w8,[x8,#0x2088]); r5->w21; action: `w8 = (w8>=0x7b)?0x10:w8`
* Confidence: high - same callee mapping, field 0x1cf8->0x2088, hooked insn mov w1,w21 is the 1:1 equivalent

## 0x4e74a4 (INLINE, 3.0.3 fn 0x4e6d94, installer FUN_00018ccc)

* **What it does**: Menu scene data field +0x1cf8 (4.0.1 +0x2088) = selected course index is loaded into r0, stored to the stack and passed by pointer to 0x8d4a28 (4.0.1 0x83ee8c). Hook replaces the next instruction `mov r1,r5` (4.0.1 `mov w1,w21`, same position): stub 0x28c90 clamps r0: idx>=0x7b -> 0x10, then does mov r1,r5.
* **4.0.1 site**: 0x4aa600 in fn 0x4a9f40; regs: r0->w8 (value just loaded by ldr w8,[x8,#0x2088]); r5->w21; action: `w8 = (w8>=0x7b)?0x10:w8`
* Confidence: high - same callee mapping, field 0x1cf8->0x2088, hooked insn mov w1,w21 is the 1:1 equivalent

## 0x4e7560 (INLINE, 3.0.3 fn 0x4e6d94, installer FUN_00018ccc)

* **What it does**: Menu scene data field +0x1cf8 (4.0.1 +0x2088) = selected course index is loaded into r0, stored to the stack and passed by pointer to 0x8d4ad0 (4.0.1 0x83ef40). Hook replaces the next instruction `mov r1,r5` (4.0.1 `mov w1,w21`, same position): stub 0x28c90 clamps r0: idx>=0x7b -> 0x10, then does mov r1,r5.
* **4.0.1 site**: 0x4aa6ac in fn 0x4a9f40; regs: r0->w8 (value just loaded by ldr w8,[x8,#0x2088]); r5->w21; action: `w8 = (w8>=0x7b)?0x10:w8`
* Confidence: high - same callee mapping, field 0x1cf8->0x2088, hooked insn mov w1,w21 is the 1:1 equivalent

## 0x40d8d8 (INLINE, 3.0.3 fn 0x40d82c, installer FUN_00018ccc)

* **What it does**: Per-player loop (16) over karts; for state 0 it loads [raceInfo2+0x1a0] (raceInfo2 = [[gbl+0x24]+0x144]+8, the 0x906fc4 object; inlined) and passes it to 0x8fd308 (4.0.1 0x865944). Stub 0x28c10: r0 = idx, if >= 0x7b then 0x10.
* **4.0.1 site**: 0x3df5a0 in fn 0x3df4f0; regs: r0(raceinfo2)->x8; r0->w8; action: `w8 = (w8>=0x7b)?0x10:w8`
* Confidence: high - 3.0.3 fn unmatched; 4.0.1 fn 0x3df4f0 is the only caller of 0x865944/0x8659a0 (= 0x8fd308/0x8fd358), identical loop

## 0x4e53ac (INLINE, 3.0.3 fn 0x4e5280, installer FUN_00018ccc)

* **What it does**: Loads course index from the 0x906fc4 race-info object (4.0.1 getter 0x86f7d4) into r7. Stub 0x28c20: r7 = idx>=0x7b ? 0x10 : idx.
* **4.0.1 site**: 0x4a8844 in fn 0x4a8710; regs: r0->x0; r7->w22; action: `w22 = (w22>=0x7b)?0x10:w22`
* Confidence: high - bl 0x86f7d4; ldr w22,[x0,#0x1a0] in matched fn

## 0x4e1454 (INLINE, 3.0.3 fn 0x4e12d0, installer FUN_00018ccc)

* **What it does**: After copying the cup-table record {course idx, platform label(+4), name label(+8), byte(+0xc)} into scene+0x1cf8, the game calls 0x452054 (4.0.1 0x41fe64) with that struct. Stub 0x28960 (before `mov r0,r5`): c=[r5]; [r5+4]=FUN_16570(c)=courseDB[c].0x18 (name label); [r5+8]=FUN_1658c(c)=courseDB[c].0x1c (platform label). NOTE: unlike every other site it indexes the DB with the course index without +1 and writes name into the platform slot (+4) and platform into the name slot (+8); looks like a plugin bug - replicate faithfully or verify in-game.
* **4.0.1 site**: 0x4a3ab4 in fn 0x4a3940; regs: r5 (=scene+0x1cf8)->x0 (=x22+0x2088); action: `[x0+4] = db.name([x0]); [x0+8] = db.platform([x0])`
* Confidence: medium - 4.0.1: ldur x8,[rec,#4]; str x8,[x22,#0x208c]; x0=x22+0x2088; bl 0x41fe64 - hook right before the bl

## 0x4e40dc (INLINE, 3.0.3 fn 0x4e37d0, installer FUN_00018ccc)

* **What it does**: Loads scene course index (+0x1cf8 / +0x2088) and stores it to [fp] (4.0.1 [x21]). Stub 0x28be0: does the load, then if [[sl+0x670]+0x64] >= 0x7b (another copy of the course index; 4.0.1 [[this+0x890]+0xb4]) replaces the value with 0x10.
* **4.0.1 site**: 0x4a6b70 in fn 0x4a5c48; regs: r0->w8 (after ldr w8,[x8,#0x2088]); sl(this)->[sp,#0x10] ; fp->x21; action: `w8 = ([[this+0x890]+0xb4] >= 0x7b) ? 0x10 : w8 ; this=[sp,#0x10]`
* Confidence: medium - ldr w8,[x8,#0x2088]; str w8,[x21] in matched fn; 0x670->0x890 and +0x64->+0xb4 seen in 4a3940 (same scene class); `this` is spilled to [sp,#0x10] at 0x4a5dc0 (x19 is reused)

## 0x5805d0 (INLINE0, 3.0.3 fn 0x5805d0, installer FUN_00018ccc)

* **What it does**: INLINE0 (absolute address). Course index loaded from race info, then `sub #0x46; cmp #4` (battle-course range check). Stub 0x28a50: r0 = db.base(idx+1).
* **4.0.1 site**: 0x53314c in fn 0x532b8c; regs: r0(raceinfo, from [sp,#0x14])->x8; r0->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - ldr w8,[x8,#0x1a0]; sub w8,#0x46; cmp #4 identical

## 0x59d25c (INLINE0, 3.0.3 fn 0x59d25c, installer FUN_00018ccc)

* **What it does**: INLINE0. Loads course index into r2 (stored to [sp,#0x20], later compared against a table count); stub 0x28ab0: r2 = db.base(idx+1).
* **4.0.1 site**: 0x54cadc in fn 0x54c740; regs: r5(raceinfo)->x21; r2->w10; action: `w10 = db.base(w10+1)`
* Confidence: high - unique 4.0.1 sequence: getter; ldr [x21,#8]; cmp #3; mov #6; ldrsw [x21,#0x180]; ldr w10,[x21,#0x1a0]; stp w9,w10 (map401 fn 0x54bc04 was wrong; 3.0.3 site is not inside 0x59c098)

## 0x58092c (INLINE, 3.0.3 fn 0x580890, installer FUN_00018ccc)

* **What it does**: Loads course index into r2 (stored to the stack). Stub 0x28a90: r2 = db.base(idx+1).
* **4.0.1 site**: 0x533408 in fn 0x533368; regs: r6(raceinfo)->x20; r2->w10; action: `w10 = db.base(w10+1)`
* Confidence: high - ldr w10,[x20,#0x1a0]; stp w9,w10,[sp,#8] in matched fn

## 0x965b8 (INLINE, 3.0.3 fn 0x965a8, installer FUN_00018ccc)

* **What it does**: Small init fn: course index stored to [this+0x2dc] (4.0.1 +0x494), then calls 0x968b0/0x96a00/0x96dac. Stub 0x28a50: db.base(idx+1).
* **4.0.1 site**: 0x8ebe8 in fn 0x8ebd0; regs: r0->x0; r0->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - 3.0.3 fn 0x965a8 not in f303; 4.0.1 fn found as the unique caller of 0x8ee74/0x8efa0/0x8f338 (= 0x968b0/0x96a00/0x96dac)

## 0x47aaf0 (INLINE, 3.0.3 fn 0x47aa04, installer FUN_00018ccc)

* **What it does**: Course info pane setter: this+0x64 (4.0.1 +0xb4) = course index from the arg; it reads the cup-table record for it. Plugin replaces `ldr r0,[r6,#8]` (cup-record name label, passed to GetMessage 0x948270) with FUN_16570([r4+0x64]+1) = courseDB.0x18 name label.
* **4.0.1 site**: 0x446154 in fn 0x446068; regs: r4->x19, [r4+0x64]->[x19+0xb4], r6->x22, r0->w0; action: `w0 = db.name([x19+0xb4]+1)`
* Confidence: high - matched fn, identical record walk (cmp 0x7b, 0x19, 8; +4/+8 loads, tbnz sign test)

## 0x47ab04 (INLINE, 3.0.3 fn 0x47aa04, installer FUN_00018ccc)

* **What it does**: Course info pane setter: this+0x64 (4.0.1 +0xb4) = course index from the arg; it reads the cup-table record for it. Plugin executes `ldr r1,[r6,#4]!` (platform label, sign-tested) then r1 = FUN_1658c([r4+0x64]+1) = courseDB.0x1c platform label.
* **4.0.1 site**: 0x446168 in fn 0x446068; regs: r4->x19, [r4+0x64]->[x19+0xb4], r6->x22, r1->w8 (keep the x22 writeback); action: `ldr w8,[x22,#4]!; w8 = db.platform([x19+0xb4]+1)`
* Confidence: high - matched fn, identical record walk (cmp 0x7b, 0x19, 8; +4/+8 loads, tbnz sign test)

## 0x47ab1c (INLINE, 3.0.3 fn 0x47aa04, installer FUN_00018ccc)

* **What it does**: Course info pane setter: this+0x64 (4.0.1 +0xb4) = course index from the arg; it reads the cup-table record for it. Plugin replaces `ldr r0,[r6]` (platform label for GetMessage) with courseDB.0x1c of [r4+0x64]+1.
* **4.0.1 site**: 0x44617c in fn 0x446068; regs: r4->x19, [r4+0x64]->[x19+0xb4], r6->x22, r0->w0; action: `w0 = db.platform([x19+0xb4]+1)`
* Confidence: high - matched fn, identical record walk (cmp 0x7b, 0x19, 8; +4/+8 loads, tbnz sign test)

## 0x451f38 (FUNC, 3.0.3 fn 0x451f38, installer FUN_00018ccc)

* **What it does**: FUNC. Original: int GetCupNameLabel(const int* cupId) -> static table[cupId] or -1. Replacement FUN_166ec: cupId = (signed char)*ptr; cup = cupDB lookup (FUN_1356c); return cup->field_4 (cup name label: 1301.. / 11601.. / 700000+n).
* **4.0.1 site**: 0x41fd6c in fn 0x41fd6c; regs: r0(ptr)->x0; ret r0->w0; action: `w0 = cupDB.find((int8)[x0]).nameLabel (cup+4)`
* Confidence: high - entry of matched fn, identical code

## 0x1c64c (INLINE, 3.0.3 fn 0x1c490, installer FUN_00018ccc)

* **What it does**: Course-name getter: final `ldr r0,[table,r4,lsl#2]` returns the course folder-name string for course id r4. Stub 0x28ca0: r0 = FUN_16708(r4) = courseDB[id].field_0 (folder name, e.g. "CT_00"). Note the getter bails out early for id > 0x7b (cmp at 3.0.3 0x1c49c / 4.0.1 0x1b4c8); that gate must also be lifted (other group) for CT ids to reach the hook. 4.0.1 returns a 64-bit pointer.
* **4.0.1 site**: 0x1b63c in fn 0x1b4ac; regs: r4->w19; r0->x0; action: `x0 = db.folder(w19) (guest char* of entry[w19].field_0; id>=count -> entry 0)`
* Confidence: high - ldr x0,[x8,w19,sxtw #3] is the same table load

## 0x4cfde8 (FUNC, 3.0.3 fn 0x4cfde8, installer FUN_00018ccc)

* **What it does**: FUNC. Original sets up the cup-select layout animation groups: InitGroups(this,11) (0x955c64 / 4.0.1 0x8bb0ac), then 11 groups via 0x955d54 (0x8bb180) + AddAnim 0x94793c (0x8ad158). Replacement thunk_FUN_36400: InitGroups(this,12) and groups 0:{CourseBGChange} 1:{TropyOnOff} 2:{RandomBtnOnOff} 3:{CupIconLineChange_00} 4:{InAll_RL,InAll_LR,OutAll_RL,OutAll_LR} 5:{Icon_08} 6:{CupIn,CupOut} 7:{ChangeIconVisible} 8:{BGBoarrdChangeWtoB,BGBoardChangeBtoW} 9:{Loop} 10:{LBtnDown,RBtnDown} 11 (new):{Scroll_RL,Scroll_LR} (spellings as in the plugin binary; they must match the CTGP layout).
* **4.0.1 site**: 0x4942b8 in fn 0x4942b8; regs: r0(layout)->x0, r1->x1; action: `reimplement: 12 anim groups (adds Scroll_RL/Scroll_LR)`
* Confidence: high - matched by identical string list

## 0x4d460c (INLINE, 3.0.3 fn 0x4d4278, installer FUN_00018ccc)

* **What it does**: On cup-select confirm the game copies cupTable[cup][slot] into scene+0x1cf8 (4.0.1 +0x2088). Stub 0x28800 replaces the course index (r2) with FUN_165a8(cup,slot): slot!=4 -> FUN_1382c(cupDB,cup,slot) = course index of that slot in CTGP's cup DB (after the optional cup remap); slot==4 -> course index from CTGP's alternate list (FUN_197e0: list[(cup/16 + off) % n]).
* **4.0.1 site**: 0x498408 in fn 0x4980a0; regs: r7(cup)->w24, r8(slot)->w25, r2->w10; action: `w10 = cupSel(w24,w25)  (before str w10,[x9,#0x2088])`
* Confidence: high - x8 = x22 + x24<<7 + x25<<4; ldr w10,[x8]; str w10,[x9,#0x2088] mirrors ldr r2,[r1,r8,lsl#4]! / str r2,[r0,#0x1cf8]

## 0x4701e4 (INLINE, 3.0.3 fn 0x4701d0, installer FUN_00018ccc)

* **What it does**: Course pane setup from a record {idx,platform,name}. Plugin replaces `ldr r0,[r5]` (course index, stored to this+0x2c8 / +0x3cc) -> FUN_1650c(idx+1): e=courseDB[id]; e.cup(+0x33)==-2 ? altList(e+0x20,e+0x31).course : cupDB[e.cup].entry[e.slot(+0x20)].course (with cup remap).
* **4.0.1 site**: 0x43c300 in fn 0x43c2e8; regs: r5->x20, r4->x19, r0->w8; action: `w8 = slotCourse([x20]+1)`
* Confidence: high - matched fn, identical loads from x20 (+0,+4,+8)

## 0x470208 (INLINE, 3.0.3 fn 0x4701d0, installer FUN_00018ccc)

* **What it does**: Course pane setup from a record {idx,platform,name}. Plugin replaces `ldr r1,[r5,#4]` (platform label, tested >0) -> FUN_164a8(idx+1): same slot resolution, returns entry +0x1c platform label.
* **4.0.1 site**: 0x43c338 in fn 0x43c2e8; regs: r5->x20, r4->x19, r1->w8; action: `w8 = slotPlatform([x20]+1)`
* Confidence: high - matched fn, identical loads from x20 (+0,+4,+8)

## 0x470224 (INLINE, 3.0.3 fn 0x4701d0, installer FUN_00018ccc)

* **What it does**: Course pane setup from a record {idx,platform,name}. Plugin replaces `ldr r0,[r5,#4]` (platform label to GetMessage) -> FUN_164a8(idx+1).
* **4.0.1 site**: 0x43c34c in fn 0x43c2e8; regs: r5->x20, r4->x19, r0->w0; action: `w0 = slotPlatform([x20]+1)`
* Confidence: high - matched fn, identical loads from x20 (+0,+4,+8)

## 0x47025c (INLINE, 3.0.3 fn 0x4701d0, installer FUN_00018ccc)

* **What it does**: Course pane setup from a record {idx,platform,name}. Plugin replaces `ldr r0,[r5,#8]` (name label; the game then adds 0x64 = title form) -> FUN_16444(idx+1): entry +0x18 name label.
* **4.0.1 site**: 0x43c384 in fn 0x43c2e8; regs: r5->x20, r4->x19, r0->w8; action: `w8 = slotName([x20]+1)`
* Confidence: high - matched fn, identical loads from x20 (+0,+4,+8)

## 0x4d268c (INLINE, 3.0.3 fn 0x4d2684, installer FUN_00018ccc)

* **What it does**: Cup-select screen update (state machine on this+0xa4, 0..5). Stub 0x28bb0 at fn+8: calls FUN_17190(this) (CTGP extra cup-row panes: shows/hides them depending on state 4/2 and a setting, calls FUN_36900) and then, if state == 6 (a new CTGP state, e.g. page scroll), returns from the function immediately; otherwise runs the original body.
* **4.0.1 site**: 0x4966c8 in fn 0x4966c8; regs: r0(this)->x0; [this+0xa4]->[this+0x124]; action: `call cupScreenUpdate(x0); if [x0+0x124]==6: return`
* Confidence: medium - matched fn; jump-table switch on [x19,#0x124] cmp 5 = [r4,#0xa4]; hook at entry (before prologue) is simplest; FUN_17190 itself reads this+0xa4, needs +0x124 in 4.0.1

## 0x5432f8 (INLINE, 3.0.3 fn 0x542e54, installer FUN_00018ccc)

* **What it does**: Grand Prix start: copies the 4 records of cupTable[cup] into scene+0x143c.. (course, platform, name, byte). Stub 0x28820: r1 = FUN_165f4(cup, off) = FUN_1382c(cupDB, cup, off>>4) = course index of CTGP cup slot (with remap).
* **4.0.1 site**: 0x4fb620 in fn 0x4fb290; regs: sl(cup)->w23, r4(slot*0x10)->x22, r1->w9; action: `after ldr w9,[x8]: w9 = cupSlotCourse(w23, x22>>4)`
* Confidence: high - cup table reader per briefing; x22 = offset 0..0x30 step 0x10, w23 = cup id from scene+0x2084

## 0x543310 (INLINE, 3.0.3 fn 0x542e54, installer FUN_00018ccc)

* **What it does**: Same loop: `ldr r1,[r0,#4]` (platform label). Stub 0x289b0: r1 = FUN_1658c([r8-4]+1) = courseDB.0x1c of the course just stored (already remapped by the 0x5432f8 hook). In 4.0.1 the +4/+8 pair is one 64-bit ldur.
* **4.0.1 site**: 0x4fb63c in fn 0x4fb290; regs: r8->x9 (x9 = x26+4+x22 = record dest), r1->low32(x8); action: `after ldur x8,[x8,#4]: c=[x9-4]; x8.lo = db.platform(c+1)`
* Confidence: high - ldur x8,[x8,#4]; str x8,[x9] = the two 32-bit loads/stores; [x9-4] = [x26+x22] just written

## 0x543314 (INLINE, 3.0.3 fn 0x542e54, installer FUN_00018ccc)

* **What it does**: Same loop: `ldr r0,[r0,#8]` (name label). Stub 0x289d0: r0 = FUN_16570([r8-4]+1) = courseDB.0x18 name label; re-does `cmp r4,#0x40` for the loop flags. Shares the 4.0.1 hook with 0x543310.
* **4.0.1 site**: 0x4fb63c in fn 0x4fb290; regs: r0->high32(x8); action: `after ldur x8,[x8,#4]: x8.hi = db.name(c+1), c=[x9-4]`
* Confidence: high - see 0x543310

## 0x44b4d0 (INLINE, 3.0.3 fn 0x44b380, installer FUN_00018ccc)

* **What it does**: Builds "UI/cmn/menu/ym_CoursePict_<name>^u.bntx": loads the picture-name string from a table by course id. Stub 0x28b70: r5 = FUN_163e4(id) = courseDB[id].field_4 (picture name, e.g. "CT_00_00"; id>=count -> 0).
* **4.0.1 site**: 0x4194c0 in fn 0x419374; regs: fp->w21 (idx), r5->x20 (64-bit ptr); action: `x20 = db.picName(w21+1)`
* Confidence: high - string-matched fn; ldr x20,[x8] after +1 index, as in 3.0.3

## 0x44b040 (INLINE, 3.0.3 fn 0x44aea0, installer FUN_00018ccc)

* **What it does**: Same picture-path builder (other variant). Stub 0x28b50: r5 = FUN_163e4([r5]+1).
* **4.0.1 site**: 0x419064 in fn 0x418ed0; regs: r5(&idx)->x21, r5(result)->x20; action: `x20 = db.picName([x21]+1)`
* Confidence: high - string-matched fn; ldr w8,[x21]; ... ldr x20,[x8]

## 0x4de824 (INLINE, 3.0.3 fn 0x4de414, installer FUN_00018ccc)

* **What it does**: Race-intro course title: `ldr r6,[sl,#8]` (name label for GetMessage). Stub 0x28900: r6 = courseDB[[fp+0x1a0]+1].0x18 (fp = 0x906fc4 race info).
* **4.0.1 site**: 0x4a16d8 in fn 0x4a133c; regs: fp(raceInfo2)->x20, r6->w21; action: `w21 = db.name([x20+0x1a0]+1)`
* Confidence: high - matched fn; x20 = getter 0x86f7d4 result, not reassigned; ldr w21,[x26,#8]

## 0x4de7ec (INLINE, 3.0.3 fn 0x4de414, installer FUN_00018ccc)

* **What it does**: Same, `ldr r6,[sl,#4]` (platform label). Stub 0x28920: r6 = courseDB.0x1c.
* **4.0.1 site**: 0x4a16a8 in fn 0x4a133c; regs: fp->x20, r6->w21; action: `w21 = db.platform([x20+0x1a0]+1)`
* Confidence: high - ldr w21,[x26,#4] in the same block

## 0x556ce0 (INLINE, 3.0.3 fn 0x55696c, installer FUN_00018ccc)

* **What it does**: Course-info screen: right after the platform text is set up (before `mov r0,r4; bl SetText`). Stub 0x28b80 calls FUN_16724(fp): label = courseDB[[raceInfo+0x1a0]+1].0x24 (author); finds panes "T_Author" and "T_AuthorHeader" (0x947fe4 / 4.0.1 0x8ad8e8) and sets their text (0x94830c / 0x8adc54) to GetMessage(label) and GetMessage(0x927c0).
* **4.0.1 site**: 0x50cfbc in fn 0x50ccc0; regs: fp(layout)->x21, singleton->x20; action: `call setAuthor(x21): T_Author=msg(db.author([x20+0x1a0]+1)), T_AuthorHeader=msg(600000)`
* Confidence: medium - position = mov x0,x26 before bl 0x8adc54 for the platform pane; layout x21 is the x0 of 0x8ad8e8 calls

## 0x556c90 (INLINE, 3.0.3 fn 0x55696c, installer FUN_00018ccc)

* **What it does**: `ldr r5,[r4,#8]` (name label). Stub 0x288c0: r5 = courseDB[[singleton+0x1a0]+1].0x18 (singleton from [sp,#0xc]).
* **4.0.1 site**: 0x50cf6c in fn 0x50ccc0; regs: [sp,#0xc](singleton)->x20, r5->w25; action: `w25 = db.name([x20+0x1a0]+1)`
* Confidence: high - ldr w25,[x24,#8]; x20 = getter result, live until 0x50d8a0

## 0x556cc8 (INLINE, 3.0.3 fn 0x55696c, installer FUN_00018ccc)

* **What it does**: `ldr r5,[r4,#4]` (platform label). Stub 0x288e0: courseDB.0x1c.
* **4.0.1 site**: 0x50cfa0 in fn 0x50ccc0; regs: singleton->x20, r5->w25; action: `w25 = db.platform([x20+0x1a0]+1)`
* Confidence: high - ldr w25,[x24,#4]

## 0x557194 (INLINE, 3.0.3 fn 0x55696c, installer FUN_00018ccc)

* **What it does**: Loads the cup icon name string (table indexed by the course's cup id clamped 0..0x17) for the "ym_CupIcon..." path. Stub 0x28a10: r2 = FUN_163b4(id): cupDB[courseDB[id].cup(+0x33)].field_8 (icon name, e.g. "Boo").
* **4.0.1 site**: 0x50d32c in fn 0x50ccc0; regs: r8(singleton)->x20, r2->x2 (64-bit ptr); action: `x2 = cupIconName([x20+0x1a0]+1)`
* Confidence: high - cmp #0x17 clamp; ldr x2,[x8,x9,lsl #3]

## 0x5575cc (INLINE, 3.0.3 fn 0x55696c, installer FUN_00018ccc)

* **What it does**: `ldr r0,[r7]` (cup id of the course, passed by pointer to GetCupNameLabel 0x451f38). Stub 0x28a30: r0 = FUN_16424(id) = (int8)courseDB[id].0x33 (cup id; -2 = custom).
* **4.0.1 site**: 0x50d6f8 in fn 0x50ccc0; regs: singleton->x20, r0->w8; action: `w8 = db.cup([x20+0x1a0]+1)`
* Confidence: high - ldr w8,[x24]; str [sp,#0x10]; bl 0x41fd6c

## 0x558750 (INLINE, 3.0.3 fn 0x55819c, installer FUN_00018ccc)

* **What it does**: Variant of 0x557194 in another screen. Stub 0x28940: r2 = FUN_163b4([[r6]+0x143c]+1) (r6 -> scene data ptr; +0x143c = first course of the GP cup copy; offset unchanged in 4.0.1, see 4fb290). In 4.0.1 the scene pointer is only in x8 at 0x50e5e8..0x50e600 (later overwritten), so capture it there (or recompute x20-0xc80-8*idx).
* **4.0.1 site**: 0x50e64c in fn 0x50e100; regs: scene([[r6]])->x8 at 0x50e600 (capture), r2->x2; action: `x2 = cupIconName([scene+0x143c]+1)`
* Confidence: medium - cmp #0x17 clamp + ldr x2,[x8,x9,lsl #3] in string-matched fn

## 0x558780 (INLINE, 3.0.3 fn 0x55819c, installer FUN_00018ccc)

* **What it does**: Variant of 0x5575cc: `ldr r0,[r5]` cup id passed to GetCupNameLabel. Stub 0x28990: r0 = FUN_16424([[r6]+0x143c]+1).
* **4.0.1 site**: 0x50e668 in fn 0x50e100; regs: scene (captured)->, r0->w8; action: `w8 = db.cup([scene+0x143c]+1)`
* Confidence: medium - ldr w8,[x20]; stur; bl 0x41fd6c

## 0x46fa74 (INLINE, 3.0.3 fn 0x46fa64, installer FUN_00018ccc)

* **What it does**: Cup pane setter: stores the cup id ([r5]) to this+0x2cc (+0x3d0). Stub 0x28b30: after the store, writes FUN_166bc(cupId) back to [r5]: CTGP cup remap (page/shuffle; returns the id unchanged for cups 12..23, when a setting is off, or when mode==0x84).
* **4.0.1 site**: 0x43bb80 in fn 0x43bb68; regs: r5->x20, r0->w8; action: `exec str w8,[x19,#0x3d0]; [x20] = remapCup(w8)`
* Confidence: high - ldr w8,[x20]; str w8,[x19,#0x3d0]; same switch on [x2]

## 0x4d0b48 (INLINE, 3.0.3 fn 0x4d0a70, installer FUN_00018ccc)

* **What it does**: `ldr r0,[fp]` cup id passed to GetCupNameLabel. Stub 0x28b20: r0 = FUN_166bc([fp]) (remapped cup id).
* **4.0.1 site**: 0x494fec in fn 0x494f08; regs: fp->x19, r0->w8; action: `w8 = remapCup([x19])`
* Confidence: high - ldr w8,[x19]; str [sp,#0x10]; bl 0x41fd6c

## 0x906df8 (INLINE, 3.0.3 fn 0x906da8, installer FUN_00018ccc)

* **What it does**: Returns a course-kind enum (1 if idx==0x33, 2 if 0x905fe4(idx), 4 if 0x66, 3 if 0x46, else 0). Stub 0x28af0: f = FUN_166a0(idx+1) = (int8)courseDB[id].0x34; if f != -1 the result is f, else the original.
* **4.0.1 site**: 0x86f610 in fn 0x86f5b0; regs: r5->x20, r4->x19 (sret x8), r1->w8; action: `f = db.flag34([x20+0x1a0]+1); if f != -1: w8 = f  (then str w8,[x19])`
* Confidence: high - matched fn identical (cmp 0x33/0x66/0x46); final str w8,[x19]

## 0x12558 (INLINE, 3.0.3 fn 0x12498, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code adds 1 and stores it as a "CourseID" int parameter. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x11cac in fn 0x11bec; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (adds 1 and stores it as a "CourseID" int parameter)

## 0x14e50 (INLINE, 3.0.3 fn 0x14d74, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x20 to pick a float constant (0xe67ffbff / FLT_MAX). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x143cc in fn 0x142d8; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x20 to pick a float constant (0xe67ffbff / FLT_MAX))

## 0x1701c (INLINE, 3.0.3 fn 0x16e38, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x20 to pick a float constant (0xe67ffbff). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x164e4 in fn 0x16348; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x20 to pick a float constant (0xe67ffbff))

## 0x19054 (INLINE, 3.0.3 fn 0x19038, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x20 to pick a float constant (0xf601fbff). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x182f8 in fn 0x17f20; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x20 to pick a float constant (0xf601fbff))

## 0x3405c (INLINE, 3.0.3 fn 0x33d98, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x10 (beq). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x32050 in fn 0x31db8; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x10 (beq))

## 0x35568 (INLINE, 3.0.3 fn 0x3555c, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x11. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x33528 in fn 0x33514; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x11)

## 0x57480 (INLINE, 3.0.3 fn 0x571c4, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x43 (first of two in the fn). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x53ad8 in fn 0x537fc; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x43 (first of two in the fn))

## 0x57edc (INLINE, 3.0.3 fn 0x571c4, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x43 (second of two in the fn). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x548ac in fn 0x537fc; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x43 (second of two in the fn))

## 0x71300 (INLINE, 3.0.3 fn 0x71240, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x12. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x6c070 in fn 0x6bfd4; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x12)

## 0x85a7c (INLINE, 3.0.3 fn 0x85250, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code adds 1 and calls the course-name getter (0x1c490 / 0x1b4ac). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x7f214 in fn 0x7eaa4; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (adds 1 and calls the course-name getter (0x1c490 / 0x1b4ac))

## 0xae588 (INLINE, 3.0.3 fn 0xae564, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code stores it to a stack slot that is passed by pointer. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0xa5200 in fn 0xa51d8; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (stores it to a stack slot that is passed by pointer)

## 0xb5e50 (INLINE, 3.0.3 fn 0xb5de8, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code stores it to [sp] and calls 0xae638 (4.0.1 0xa52a8) with its address. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track. (3.0.3 fn 0xb5de8 is missing from f303; 4.0.1 fn found via callees 0xac508/0xa52a8; f401 lists the fn as 0xac21c, real start ~0xac468)
* **4.0.1 site**: 0xac4d4 in fn 0xac468; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (stores it to [sp] and calls 0xae638 (4.0.1 0xa52a8) with its address)

## 0xc3788 (INLINE, 3.0.3 fn 0xc3724, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code is compared against entry[+8] of 0x3f records, later used with 0x7ffff/0xbcd checks. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track. (4.0.1 fn 0xb8ea4 is missing from f401 / merged into 0xb8e28; found as the only caller of 0xbab50 = 3.0.3 0xc5414)
* **4.0.1 site**: 0xb8f0c in fn 0xb8ea4; regs: r0(singleton)->x0; r0(result)->w9; action: `w9 = db.base(w9+1)`
* Confidence: high - bl 0x86f7bc; ldr w9,[x0,#0x1a0] in matched fn, same following use (is compared against entry[+8] of 0x3f records, later used with 0x7ffff/0xbcd checks)

## 0xc4a70 (INLINE, 3.0.3 fn 0xc4a40, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code stores it to the stack and calls 0xae638 (4.0.1 0xa52a8). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0xba22c in fn 0xba1f8; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (stores it to the stack and calls 0xae638 (4.0.1 0xa52a8))

## 0xd8f08 (INLINE, 3.0.3 fn 0xd8e10, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x3d (bne). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track. (3.0.3 fn 0xd8e10 not in f303; 4.0.1 site found by the identical str [obj+0xc/0x10] / cmp #0x3d / reload sequence; f401 boundary 0xcc498 is wrong, real start ~0xcc4d8)
* **4.0.1 site**: 0xcc5c4 in fn 0xcc4d8; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x3d (bne))

## 0xde108 (INLINE, 3.0.3 fn 0xde0f8, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x18 to choose 30.0f (also 7.0f constants follow). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track. (unmatched fn; 4.0.1 fn 0xd142c has identical cmp #0x18, 30.0, 7.0 code)
* **4.0.1 site**: 0xd1444 in fn 0xd142c; regs: r0(singleton)->x0; r0(result)->w9; action: `w9 = db.base(w9+1)`
* Confidence: high - bl 0x86f7bc; ldr w9,[x0,#0x1a0] in matched fn, same following use (compares with 0x18 to choose 30.0f (also 7.0f constants follow))

## 0xde190 (INLINE, 3.0.3 fn 0xde184, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x3f then 0x18 and writes 0xc5a0f800 / 0x432f0000 floats. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track. (unmatched fn; 4.0.1 fn 0xd14d0 identical)
* **4.0.1 site**: 0xd14e4 in fn 0xd14d0; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x3f then 0x18 and writes 0xc5a0f800 / 0x432f0000 floats)

## 0xe698c (INLINE, 3.0.3 fn 0xe6888, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code adds 1 and stores it to [this+0xe50]. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0xd8638 in fn 0xd8530; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (adds 1 and stores it to [this+0xe50])

## 0xe7470 (INLINE, 3.0.3 fn 0xe73d0, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code ORs 4 and compares with 0x25. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0xd905c in fn 0xd8fc4; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (ORs 4 and compares with 0x25)

## 0x119d44 (INLINE, 3.0.3 fn 0x119cc0, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code stores it and stores +1. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x105bdc in fn 0x105b20; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (stores it and stores +1)

## 0x11b2b8 (INLINE, 3.0.3 fn 0x11b0fc, installer FUN_00018ccc)

* **What it does**: Loads the current course index into r1 and compares it with 0x25. The plugin reuses the generic stub 0x28a50, which computes db.base(idx+1) into r0 but restores r1 from the stack, so r1 keeps whatever the preceding virtual call (blx r1) left there and the next instruction `mov r0,#0` discards the result. In 3.0.3 the check is therefore effectively broken (compares junk). Intended action: course index replaced by db.base(idx+1).
* **4.0.1 site**: 0x1073c8 in fn 0x107238; regs: r0(singleton)->x0; r1(dest)->w8; action: `w8 = db.base(w8+1)  [plugin bug: stub writes r0, r1 keeps junk]`
* Confidence: medium - bl 0x86f7bc; ldr w8,[x0,#0x1a0]; cmp w8,#0x25 (matched fn, cmp 0x25 then 0x78 as in 3.0.3)

## 0x11b2cc (INLINE, 3.0.3 fn 0x11b0fc, installer FUN_00018ccc)

* **What it does**: Same as 0x11b2b8, second check (cmp r1,#0x78). Same plugin bug: the stub result lands in r0 and is overwritten by `mov r0,#0`; r1 is whatever it held before (junk). Intended: db.base(idx+1).
* **4.0.1 site**: 0x10748c in fn 0x107238; regs: r0(singleton)->x0; r1(dest)->w8; action: `w8 = db.base(w8+1)  [plugin bug: stub writes r0, r1 keeps junk]`
* Confidence: medium - second bl 0x86f7bc; ldr w8,[x0,#0x1a0]; cmp w8,#0x78

## 0x13e914 (INLINE, 3.0.3 fn 0x13e544, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code adds 1 and stores it as "CourseID" (mov r1,#0; mov r2,#4 pattern). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x1291f8 in fn 0x128e28; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (adds 1 and stores it as "CourseID" (mov r1,#0; mov r2,#4 pattern))

## 0x1542fc (INLINE, 3.0.3 fn 0x153a4c, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code is used with float moves s18/s20 (same in 4.0.1 v9/v10). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x13e098 in fn 0x13d778; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (is used with float moves s18/s20 (same in 4.0.1 v9/v10))

## 0x166c6c (INLINE, 3.0.3 fn 0x1666f8, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x25 (first of three in the fn). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x14ecd0 in fn 0x14e7d4; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x25 (first of three in the fn))

## 0x166c7c (INLINE, 3.0.3 fn 0x1666f8, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x78 (second of three). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x14ece0 in fn 0x14e7d4; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x78 (second of three))

## 0x17fb2c (INLINE, 3.0.3 fn 0x17f360, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code adds 1 and stores it as "CourseID". Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x166c18 in fn 0x1664b0; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (adds 1 and stores it as "CourseID")

## 0x184ad8 (INLINE, 3.0.3 fn 0x184a00, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code stores it to a stack slot. Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x16c0c8 in fn 0x16bff0; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (stores it to a stack slot)

## 0x870180 (INLINE, 3.0.3 fn 0x870058, installer FUN_00018ccc)

* **What it does**: Original: `ldr r0,[r8,#0x1a0]` (r8 = race info) - stores the course index to the stack and calls 0x86fd5c(this+0x120, &idx). The plugin uses the generic stub 0x28a50 which does `ldr r0,[r0,#0x1a0]`, but here r0 is the freshly allocated 0x24-byte-element array just stored at [r4+0xfc] (or 0 if allocation failed), not the race info. So in 3.0.3 it reads junk from the heap (or crashes on null). Intended: db.base(courseIndex+1).
* **4.0.1 site**: 0x7ddfd4 in fn 0x7ddea8; regs: r8(singleton)->x20; r0(dest)->w8; action: `w8 = db.base(w8+1)  [plugin bug: stub reads [r0+0x1a0], r0 = new buffer]`
* Confidence: medium - matched fn (score) and identical sequence: alloc array, str to obj, ldr w8,[x20,#0x1a0]; str to stack; bl 0x7ddbe8

## 0x28629c (INLINE, 3.0.3 fn 0x28628c, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code adds 1 and stores it to [this+0x18c] (4.0.1 +0x240) as "CourseID". Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track. (unmatched fn: 6 identical "CourseID" functions in each build; 4th by address order, consistent field offsets 0x18c->0x240)
* **4.0.1 site**: 0x264d8c in fn 0x264d70; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (adds 1 and stores it to [this+0x18c] (4.0.1 +0x240) as "CourseID")

## 0x13c28c (INLINE, 3.0.3 fn 0x13c188, installer FUN_00018ccc)

* **What it does**: Loads the course index into sb, compared with 0x3f then 0x31. Dedicated stub 0x28ad0: sb = db.base(idx+1).
* **4.0.1 site**: 0x126e90 in fn 0x126d88; regs: r0(singleton)->x0; sb(dest)->w22; action: `w22 = db.base(w22+1)`
* Confidence: high - bl 0x86f7bc; ldr w22,[x0,#0x1a0]; cmp w22,#0x3f in matched fn

## 0x965910 (INLINE0, 3.0.3 fn 0x965910, installer FUN_00018ccc)

* **What it does**: Reads the current course index [raceInfo+0x1a0] right after the race-info getter (3.0.3 0x906fb4 / 4.0.1 0x86f7bc); the following code compares with 0x63 (blt). Plugin stub 0x28a50: r0 = FUN_16618([r0+0x1a0]+1) = db.base(courseIndex+1), i.e. the base course index of the entry, so per-course code treats a CT like its base track.
* **4.0.1 site**: 0x8caa70 in fn 0x8ca9fc; regs: r0(singleton)->x0; r0(result)->w8; action: `w8 = db.base(w8+1)`
* Confidence: high - bl 0x86f7bc; ldr w8,[x0,#0x1a0] in matched fn, same following use (compares with 0x63 (blt))

## 0x3ac31c (INLINE0, 3.0.3 fn 0x3ac31c, installer FUN_00018ccc)

* **What it does**: Debug/result text builder ("EndTime" etc.): loads course index, if ==1 uses a string at raceinfo+0x1a4 else calls the course-name getter with idx+1. Stub 0x28a70: r0 = FUN_16654(idx+1): base course index; additionally if id > 0x7b and base > 0x4a (a BCP course) returns 0x10.
* **4.0.1 site**: 0x385cb0 in fn 0x3858ac; regs: r7(raceinfo)->x22; r0->w8; action: `w8 = db.baseBCP(w8+1)`
* Confidence: high - mov w8,#4; str [sp]; ldr w8,[x22,#0x1a0]; cmp #1; add x4,x22,#0x1a4; else +1, bl 0x1b4ac - identical (3.0.3 fn unmatched; f401 boundary wrong, start ~0x3858ac)

## 0x556864 (INLINE, 3.0.3 fn 0x556168, installer FUN_00018ccc)

* **What it does**: End of a texture-preload function (after the last 0x9480d0 call, at `add sp,#8`). Stub 0x289f0: FUN_16844(r4): for each cup in CTGP's cup DB, builds "UI/cmn/race/ym_CupIcon<icon>_130x130^h.bntx" and "..._Line^s.bntx" and for each calls 0x9480f8 (4.0.1 0x8ada3c) and, if that returns 0, 0x9480d0 (4.0.1 0x8ada10) with {vtable, path} - i.e. registers the CTGP cup icon textures.
* **4.0.1 site**: 0x50ccb0 in fn 0x50c6a8; regs: r4->x19 (=this+0x30); action: `call preloadCupIcons(x19)`
* Confidence: high - matched fn ends with two bl 0x8ada10 on x19; hook at the epilogue

