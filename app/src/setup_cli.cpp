// SPDX-License-Identifier: GPL-3.0-or-later
// kartrecomp-setup: the Setup page's whole pipeline from a shell (acceptance runs, support).
//   kartrecomp-setup --base <base.nsp> --update <update.nsp> --keys <prod.keys> --firmware <dir|zip> [--dlc <x.nsp>]...
// Uses the XDG data folders (or XDG_DATA_HOME / XDG_CACHE_HOME). Exit codes: 0 built and verified, 1 failed (reason on stderr).
#include <QCoreApplication>
#include <cstdio>

#include "setup_pipeline.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments().mid(1);
    SetupInputs in;
    for (int i = 0; i + 1 < a.size(); i += 2) {
        if (a[i] == "--base") in.base = a[i + 1];
        else if (a[i] == "--update") in.update = a[i + 1];
        else if (a[i] == "--keys") in.keys = a[i + 1];
        else if (a[i] == "--firmware") in.firmware = a[i + 1];
        else if (a[i] == "--dlc") in.dlc << a[i + 1];
        else { std::fprintf(stderr, "unknown argument %s\n", qPrintable(a[i])); return 1; }
    }
    if (in.base.isEmpty() || in.update.isEmpty() || in.keys.isEmpty() || in.firmware.isEmpty()) {
        std::fprintf(stderr, "usage: kartrecomp-setup --base <base.nsp> --update <update.nsp> --keys <prod.keys> --firmware <dir|zip> [--dlc <x.nsp>]...\n");
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
