// SPDX-License-Identifier: GPL-3.0-or-later
// kartrecomp-build: command-line front end of BuildRunner (CI, acceptance runs, support).
//   kartrecomp-build <export dir> <work dir> <out dir> (--toolchain <dir> | --compiler <clang>) [--hot <list>] [--jobs <n>] [-- <extra args>]
// Exit codes: 0 ok, 1 build failed, 2 bad input or not enough RAM/disk.
#include <QCoreApplication>
#include <QFile>
#include <QStorageInfo>
#include <QThread>
#include <cstdio>

#include "build_runner.h"

namespace {
qint64 AvailableRam() { return BuildRunner::AvailableMemoryBytes(); }
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments().mid(1);
    BuildPlan plan;
    QStringList positional;
    int jobs = 0;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i] == "--compiler" && i + 1 < a.size()) plan.compiler = a[++i];
        else if (a[i] == "--toolchain" && i + 1 < a.size()) plan.useToolchain(a[++i]);
        else if (a[i] == "--hot" && i + 1 < a.size()) plan.hotList = a[++i];
        else if (a[i] == "--jobs" && i + 1 < a.size()) jobs = a[++i].toInt();
        else if (a[i] == "--") { plan.compileArgs += a.mid(i + 1); plan.linkArgs += a.mid(i + 1); break; }
        else positional << a[i];
    }
    if (positional.size() != 3 || plan.compiler.isEmpty()) {
        std::fprintf(stderr, "usage: kartrecomp-build <export dir> <work dir> <out dir> (--toolchain <dir> | --compiler <clang>) [--hot <list>] [--jobs <n>] [-- <args>]\n");
        return 2;
    }
    plan.exportDir = positional[0];
    plan.workDir = positional[1];
    plan.outDir = positional[2];
    const int by_ram = BuildRunner::JobsForRam(AvailableRam(), QThread::idealThreadCount());
    if (by_ram == 0) {
        std::fprintf(stderr, "Not enough free memory: the build needs at least 3 GB free (close other programs and try again).\n");
        return 2;
    }
    plan.jobs = jobs > 0 ? jobs : by_ram;
    if (!BuildRunner::HasDiskSpace(plan.workDir.isEmpty() ? "." : plan.workDir, 6LL << 30) && QStorageInfo(plan.workDir).isValid()) {
        std::fprintf(stderr, "Not enough free disk space in %s: the build needs about 6 GB.\n", qPrintable(plan.workDir));
        return 2;
    }
    const auto result = BuildRunner::Build(plan, [](int done, int total, const QString& what) {
        std::printf("PROGRESS %d %d %s\n", done, total, qPrintable(what));
        std::fflush(stdout);
    });
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", qPrintable(result.message));
        return 1;
    }
    return 0;
}
