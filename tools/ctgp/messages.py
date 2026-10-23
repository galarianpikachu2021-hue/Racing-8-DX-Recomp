#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adds the names of CTGP DX's tracks to the game's own message files as NEW labels (the game's existing labels are not touched).

    messages.py <game message dump dir> <CTGPDX zip> <mod dir>

<game message dump dir> holds UI/<lang>/message.sarc as dumped from the user's game (KARTRECOMP_ROMFS_DUMP, no mods). For each of the 15 languages
the game's Common.msbt gets, for track j (CT_xx hex, 0..63): label 21000+j = the track's name and 21100+j = its title form. The names are taken from the
pack's own text by the track's English name (CTGP_LAYOUT). Output: <mod dir>/romfs/UI/<lang>/message.sarc.
"""
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import slots  # noqa: E402
from msbt import Msbt  # noqa: E402
from sarc import Sarc  # noqa: E402

PREFIX = "atmosphere/contents/0100152000022000/romfs/"
NAME_BASE, TAG_OFFSET = 21000, 100


def norm(text: str) -> str:
    return "".join(c for c in text.replace("’", "'").lower() if c.isalnum())


def text_of(m: Msbt, label: str) -> str:
    return (m.text(label) or "").rstrip("\0")


def main():
    dump, zpath, mod = sys.argv[1:4]
    with zipfile.ZipFile(zpath) as z:
        names = set(z.namelist())
        # English decides which label is which track; every language then reads that label.
        en = Msbt(Sarc(z.read(PREFIX + "UI/USen/message.sarc")).files["CTGPText.msbt"])
        en_by_name: dict[str, str] = {}
        for label in sorted(en.labels):
            if label.isdigit() and 500100 <= int(label) <= 500199:
                en_by_name.setdefault(norm(text_of(en, label)), label)
        track_label = {j: en_by_name.get(norm(t)) for j, t in enumerate(slots.CTGP_LAYOUT)}
        # A few CTGP tracks keep the game's own names (DK Jungle, Mario Circuit, Rainbow Road): found by English text in the game's labels.
        van_en = Msbt(Sarc(open(os.path.join(dump, "UI", "USen", "message.sarc"), "rb").read()).files["Common.msbt"])
        game_by_name: dict[str, str] = {}
        for n in range(1401, 1497):
            if str(n) in van_en.labels:
                game_by_name.setdefault(norm(text_of(van_en, str(n))), str(n))
        game_label = {j: game_by_name.get(norm(t)) for j, t in enumerate(slots.CTGP_LAYOUT) if track_label[j] is None}
        missing = [slots.CTGP_LAYOUT[j] for j, l in game_label.items() if l is None]
        if missing:
            print("no label for:", missing)
        for lang in slots.LANGUAGES:
            van_path = os.path.join(dump, "UI", lang, "message.sarc")
            if not os.path.exists(van_path):
                continue
            member = PREFIX + f"UI/{lang}/message.sarc"
            pack = Sarc(z.read(member if member in names else PREFIX + "UI/USen/message.sarc"))
            ctext = Msbt(pack.files["CTGPText.msbt"])
            van = Sarc(open(van_path, "rb").read())
            common = Msbt(van.files["Common.msbt"])
            added = 0
            for j, track in enumerate(slots.CTGP_LAYOUT):
                label = track_label[j]
                if label is None:
                    g = game_label.get(j)
                    if g is None:
                        continue
                    tagged = str(int(g) + TAG_OFFSET)
                    text = text_of(common, g)
                    common.add_label(str(NAME_BASE + j), text)
                    common.add_label(str(NAME_BASE + TAG_OFFSET + j), text_of(common, tagged) if tagged in common.labels else text)
                    added += 1
                    continue
                tagged = str(int(label) + TAG_OFFSET)
                common.add_label(str(NAME_BASE + j), text_of(ctext, label))
                common.add_label(str(NAME_BASE + TAG_OFFSET + j), text_of(ctext, tagged) if tagged in ctext.labels else text_of(ctext, label))
                added += 1
            van.files["Common.msbt"] = common.write()
            out = os.path.join(mod, "romfs", "UI", lang, "message.sarc")
            os.makedirs(os.path.dirname(out), exist_ok=True)
            open(out, "wb").write(van.write())
            print(lang, added, "track names")


if __name__ == "__main__":
    main()
