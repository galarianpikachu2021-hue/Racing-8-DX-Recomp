#!/usr/bin/env python3
"""make_ts.py <strings.json> <translations.py> <lang> <out.ts>: writes a Qt translation file for the launcher.

strings.json: [[context, source, numerus], ...] (the launcher's tr() texts, from lupdate plus the run-time ones).
translations.py: defines T = {index: text}; a missing index stays untranslated (the English text is shown).
Checks that every translation keeps the source's placeholders (%1.., %n, %v, %m) and line breaks."""
import html, json, re, runpy, sys

FORMS = {"ja": 1, "zh_CN": 1, "zh_TW": 1, "ko": 1, "ru": 3}

def main():
    strings = json.load(open(sys.argv[1], encoding="utf-8"))
    T = runpy.run_path(sys.argv[2])["T"]
    lang, out = sys.argv[3], sys.argv[4]
    bad = 0
    ph = re.compile(r"%(?:\d|n|v|m)")
    for i, t in T.items():
        if i >= len(strings):
            print(f"{lang}: index {i} beyond {len(strings)} strings"); bad += 1; continue
        src = strings[i][1]
        if sorted(ph.findall(src)) != sorted(ph.findall(t)) or src.count("\n") != t.count("\n"):
            print(f"{lang} {i}: placeholders differ\n  {src!r}\n  {t!r}"); bad += 1
    by_ctx = {}
    for i, (ctx, src, numerus) in enumerate(strings):
        for c in ([ctx, "QDialog"] if ctx == "PressDialog" else [ctx]):  # PressDialog has no Q_OBJECT: tr() looks in QDialog
            by_ctx.setdefault(c, []).append((src, numerus, T.get(i)))
    e = lambda s: html.escape(s, quote=False)
    lines = ['<?xml version="1.0" encoding="utf-8"?>', "<!DOCTYPE TS>", f'<TS version="2.1" language="{lang}">']
    for ctx, msgs in by_ctx.items():
        lines += ["<context>", f"    <name>{ctx}</name>"]
        seen = set()
        for src, numerus, tr in msgs:
            if src in seen: continue
            seen.add(src)
            lines.append('    <message numerus="yes">' if numerus else "    <message>")
            lines.append(f"        <source>{e(src)}</source>")
            if tr is None:
                lines.append('        <translation type="unfinished"></translation>')
            elif numerus:
                lines.append("        <translation>" + "".join(f"<numerusform>{e(tr)}</numerusform>" for _ in range(FORMS.get(lang, 2))) + "</translation>")
            else:
                lines.append(f"        <translation>{e(tr)}</translation>")
            lines.append("    </message>")
        lines.append("</context>")
    lines.append("</TS>")
    open(out, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print(f"{lang}: {len(T)} of {len(strings)} translated, {bad} problem(s)")
    sys.exit(1 if bad else 0)

main()
