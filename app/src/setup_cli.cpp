// SPDX-License-Identifier: GPL-3.0-or-later
// kartrecomp-setup: the Setup page's whole pipeline from a shell (acceptance runs, support).
//   kartrecomp-setup --base <base.nsp> --update <update.nsp> --keys <prod.keys> --firmware <dir|zip> [--dlc <x.nsp>]...
//   kartrecomp-setup menu-merge <baseline hashes> <game UI/cmn dir> <out mod dir> <mod dir>...   (support: the launcher's menu merge)
//   kartrecomp-setup ctgp-convert <CTGP zip> <game UI dump dir> <out mod dir> <baseline hashes>   (support: the launcher's CTGP import)
// Uses the XDG data folders (or XDG_DATA_HOME / XDG_CACHE_HOME). Exit codes: 0 built and verified, 1 failed (reason on stderr).
#include <QCoreApplication>
#include <cstdio>

#include "ctgp_convert.h"
#include "menu_merge.h"
#include "setup_pipeline.h"
#include "storage.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments().mid(1);
    if (a.value(0) == QLatin1String("ctgp-convert") && a.size() == 5) {
        CtgpConvert::Options options;
        options.vanilla_cmn_dir = a[2] + QStringLiteral("/UI/cmn");
        options.baseline_path = a[4];
        QString error;
        const bool ok = CtgpConvert::Convert(a[1], a[2], a[3], options, &error, [](int done, int total, const QString&) {
            if (done % 100 == 0) std::printf("%d/%d\n", done, total);
            return true;
        });
        if (!ok) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
        std::printf("converted\n");
        return 0;
    }
    if (a.value(0) == QLatin1String("menu-merge") && a.size() >= 5) {
        MenuMerge::Baseline base;
        if (!base.load(a[1])) { std::fprintf(stderr, "cannot read %s\n", qPrintable(a[1])); return 1; }
        QString error;
        QStringList report;
        const int n = MenuMerge::Merge(a.mid(4), a[2], base, a[3], &error, &report);
        for (const QString& line : report) std::printf("%s\n", qPrintable(line));
        if (n < 0) { std::fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
        std::printf("%d archive(s) written\n", n);
        return 0;
    }
    SetupInputs in;
    for (int i = 0; i + 1 < a.size(); i += 2) {
        if (a[i] == "--base") in.base = a[i + 1];
        else if (a[i] == "--update") in.update = a[i + 1];
        else if (a[i] == "--keys") in.keys = a[i + 1];
        else if (a[i] == "--firmware") in.firmware = a[i + 1];
        else if (a[i] == "--dlc") in.dlc << a[i + 1];
        else if (a[i] == "--patch") in.patches << a[i + 1];
        else if (a[i] == "--exefs") in.exefs = a[i + 1];
        else if (a[i] == "--romfs") in.romfs = a[i + 1];
        else { std::fprintf(stderr, "unknown argument %s\n", qPrintable(a[i])); return 1; }
    }
    const bool nsp = !in.base.isEmpty() && !in.update.isEmpty() && !in.keys.isEmpty() && !in.firmware.isEmpty();
    const bool decrypted = !in.exefs.isEmpty() && !in.romfs.isEmpty();
    if (!nsp && !decrypted) {
        std::fprintf(stderr, "or (decrypted files, no keys or firmware): kartrecomp-setup --exefs <code folder> --romfs <data folder or romfs.bin>\n");
        std::fprintf(stderr, "usage: kartrecomp-setup --base <base.nsp> --update <update.nsp> --keys <prod.keys> --firmware <dir|zip> [--dlc <x.nsp>]... [--patch split60]\n");
        return 1;
    }
    if (Storage::Missing()) {
        std::fprintf(stderr, "The game is kept in %s (the launcher's Storage tab), which is not there.\n", qPrintable(Storage::Chosen()));
        return 1;
    }
    const auto result = RunSetupPipeline(in, DataLayout::Default(), BuilderPaths::Detect(), [](int permille, const QString& text) {
        std::printf("PROGRESS %d %s\n", permille, qPrintable(text));
        std::fflush(stdout);
    }, nullptr);
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", qPrintable(result.message));
        return 1;
    }
    return 0;
}
