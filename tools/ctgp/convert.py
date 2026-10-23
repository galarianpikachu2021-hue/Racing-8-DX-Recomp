#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Converts a CTGP Deluxe pack (the user's own zip) into a data mod for the static (no-JIT) build.

The pack's code (a 32-bit Skyline plugin for game 3.0.3) cannot run here, so each custom track takes the place of one of the game's own
race courses: the track's files go into that course's romfs folder, its thumbnail and music take over the course's names, and the
course / cup names in the game's message files are replaced with the pack's. Nothing from the pack is kept except the converted mod.

    convert.py --pack "CTGPDX v1.1.1.zip" --vanilla-ui <dir with UI/<lang>/message.sarc from your game> --out <mods/<tid>/CTGP DX (tracks)>
               [--cups 12]   how many CTGP DX cups (4 tracks each, in the pack's order) to convert, at most 12
"""
import argparse
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
from msbt import Msbt  # noqa: E402
from sarc import Sarc  # noqa: E402
import slots  # noqa: E402

PRE = "atmosphere/contents/0100152000022000/romfs/"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pack", required=True)
    ap.add_argument("--vanilla-ui", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cups", type=int, default=12)
    ap.add_argument("--first-cup", type=int, default=0)
    args = ap.parse_args()
    z = zipfile.ZipFile(args.pack)
    names = {i.filename[len(PRE):]: i for i in z.infolist() if i.filename.startswith(PRE) and not i.is_dir()}

    def put(rel: str, data: bytes):
        path = os.path.join(args.out, "romfs", rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)

    plan = []  # (cup index, slot name, music, label, ct id)
    for c, (cup_name, cup_label, icon, courses) in enumerate(slots.CUPS[: min(args.cups, 12)]):
        for r, (slot, music, label) in enumerate(courses):
            ct = (args.first_cup + c) * 4 + r
            if slot is None or ct > 63:
                continue
            plan.append((c, slot, music, label, ct))

    # 1. courses, thumbnails, music
    for c, slot, music, label, ct in plan:
        src = f"Course/CT_{ct:02X}/"
        files = [n for n in names if n.startswith(src)]
        banks = [n for n in files if n.count("/") == 2 and re.match(r".*/Course_[^/]+\.bars$", n)]
        barslist = [n for n in files if n.count("/") == 2 and n.endswith(".barslist")]
        targets = [(slot, music)]
        if slot == "Du_Animal_Spring":
            targets += slots.ANIMAL_CROSSING_EXTRA
        for folder, folder_music in targets:
            for n in files:
                rel = n[len(src):]
                if n in banks or n in barslist:
                    continue
                put(f"Course/{folder}/{rel}", z.read(names[n]))
            if len(banks) == 1:
                put(f"Course/{folder}/Course_{folder}.bars", z.read(names[banks[0]]))
                if barslist:
                    text = z.read(names[barslist[0]])
                    text = re.sub(rb"Course_[A-Za-z0-9_]+", b"Course_" + folder.encode(), text)
                    put(f"Course/{folder}/Course_{folder}.barslist", text)
            stream_prefix = f"Audio/Resource/Stream/pBGM_CT_{ct:02X}_"
            for n in names:
                if n.startswith(stream_prefix) and n.endswith(".bfstm"):
                    put(f"Audio/Resource/Stream/pBGM_{folder_music}_{n[len(stream_prefix):]}", z.read(names[n]))
        pict_name = slots.ANIMAL_CROSSING_PICTURE if slot == "Du_Animal_Spring" else slot
        pict = f"UI/cmn/menu/ym_CoursePict_CT_{ct:02X}_00^u.bntx"
        if pict in names:
            put(f"UI/cmn/menu/ym_CoursePict_{pict_name}_00^u.bntx", z.read(names[pict]))
        print(f"CT_{ct:02X} -> {slot}")

    # 2. cup icons
    for c, (cup_name, cup_label, icon, _) in enumerate(slots.CUPS[: min(args.cups, 12)]):
        ct_icon = slots.CTGP_CUP_ICONS[args.first_cup + c]
        for v in slots.ICON_VARIANTS:
            src = f"UI/cmn/race/ym_CupIcon{ct_icon}_{v}.bntx"
            if src in names:
                put(f"UI/cmn/race/ym_CupIcon{icon}_{v}.bntx", z.read(names[src]))

    # 3. names, in every language the game has. Each track's name is found by its text (English layout name) among the pack's
    #    name labels 5001xx; the title form is the same label + 100.
    def norm(text: str) -> str:
        return re.sub(r"[^a-z0-9]", "", text.replace("\u2019", "'").lower())

    for lang in slots.LANGUAGES:
        van_path = os.path.join(args.vanilla_ui, "UI", lang, "message.sarc")
        pack_path = f"UI/{lang}/message.sarc"
        if not os.path.exists(van_path):
            continue
        van = Sarc(open(van_path, "rb").read())
        pack = Sarc(z.read(names[pack_path])) if pack_path in names else Sarc(z.read(names["UI/USen/message.sarc"]))
        common = Msbt(van.files["Common.msbt"])
        ctext = Msbt(pack.files["CTGPText.msbt"])
        # English names decide which label belongs to which layout name (other languages share the label numbers)
        en = Msbt(Sarc(z.read(names["UI/USen/message.sarc"])).files["CTGPText.msbt"])
        by_name = {}
        for lab in sorted(int(k) for k in en.labels if k.isdigit() and 500100 <= int(k) <= 500199):
            by_name.setdefault(norm((en.text(str(lab)) or "").strip("\x00")), []).append(lab)
        # a few CTGP tracks keep the game's own course names ("DK Jungle", "Mario Circuit", "Rainbow Road"): take the game's text
        van_en = Msbt(Sarc(open(os.path.join(args.vanilla_ui, "UI", "USen", "message.sarc"), "rb").read()).files["Common.msbt"])
        van_by_name = {}
        for lab in sorted(int(k) for k in van_en.labels if k.isdigit() and 1401 <= int(k) <= 1496):
            van_by_name.setdefault(norm((van_en.text(str(lab)) or "").strip("\x00")), []).append(lab)
        original = {k: v for k, v in zip(common.labels, [common.strings[i] for i in common.labels.values()])}
        for c, slot, music, label, ct in plan:
            want = norm(slots.CTGP_LAYOUT[ct])
            cands = by_name.get(want, [])
            if cands:
                lab_c = cands[0]
                for lab_v, src in ((label, lab_c), (label + 100, lab_c + 100)):
                    if str(src) in ctext.labels and str(lab_v) in common.labels:
                        common.strings[common.labels[str(lab_v)]] = ctext.strings[ctext.labels[str(src)]]
            elif want in van_by_name:
                src = van_by_name[want][0]
                for lab_v, s in ((label, src), (label + 100, src + 100)):
                    if str(s) in original and str(lab_v) in common.labels:
                        common.strings[common.labels[str(lab_v)]] = original[str(s)]
            else:
                print("no name for", ct, slots.CTGP_LAYOUT[ct])
        for c in range(min(args.cups, 12)):
            lab_c, lab_v = str(700000 + args.first_cup + c), str(slots.CUPS[c][1])
            if lab_c in ctext.labels and lab_v in common.labels:
                common.strings[common.labels[lab_v]] = ctext.strings[ctext.labels[lab_c]]
        van.files["Common.msbt"] = common.write()
        put(f"UI/{lang}/message.sarc", van.write())
    print("done:", len(plan), "tracks")


if __name__ == "__main__":
    main()
