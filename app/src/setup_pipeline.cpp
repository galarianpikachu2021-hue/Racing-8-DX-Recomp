// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_pipeline.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QThread>

#include "build_runner.h"
#include "nsp_info.h"
#include "verify_build.h"

namespace {
qint64 AvailableRam() { return BuildRunner::AvailableMemoryBytes(); }
bool Cancelled(const std::atomic<bool>* c) { return c && c->load(); }
QString FileText(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
QString FileHash(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex().left(16));
}
} // namespace

PipelineResult RunSetupPipeline(const SetupInputs& in, const DataLayout& layout, const BuilderPaths& paths,
                                const std::function<void(int, const QString&)>& progress, const std::atomic<bool>* cancel, const PipelineLimits& limits) {
    const auto report = [&](int permille, const QString& text) { if (progress) progress(permille, text); };
    QString missing;
    if (!paths.complete(&missing)) return {false, missing};

    report(0, QCoreApplication::translate("Messages", "Checking your files"));
    const auto reader = [&](const QString& p) { return ReadNspInfo(paths.exporter, p, in.keys); };
    if (const QString why = ValidateInputs(in, reader); !why.isEmpty()) return {false, why};
    // Each supported update has its own hot block list (data/mk8d-<version>/hot_blocks.txt); without one every block is compiled cold.
    const QString updateName = in.decrypted() ? DecryptedUpdateName(in.exefs) : UpdateName(reader(in.update).version);
    QString hotList = HotListFor(paths.hotList, updateName);
    if (!QFileInfo::exists(hotList)) hotList.clear();
    if (Cancelled(cancel)) return {false, QStringLiteral("Cancelled.")};
    const int jobs = BuildRunner::JobsForRam(AvailableRam(), QThread::idealThreadCount());
    if (jobs == 0) return {false, QCoreApplication::translate("Messages", "Not enough free memory: close other programs (the build needs at least 3 GB free) and try again.")};
    // The build's work files (about 6 GB) go to the cache folder; the finished game (about 1.5 GB) and the installed files to the data folder.
    if (!BuildRunner::HasDiskSpace(layout.cacheRoot, limits.cacheBytes))
        return {false, QCoreApplication::translate("Messages", "Not enough free disk space in %1: the build needs about 7 GB there.").arg(layout.cacheRoot)};
    if (!BuildRunner::HasDiskSpace(layout.dataRoot, limits.dataBytes))
        return {false, QCoreApplication::translate("Messages", "Not enough free disk space in %1: the game needs about 2 GB there.").arg(layout.dataRoot)};

    report(5, QCoreApplication::translate("Messages", "Installing your files"));
    if (const QString err = InstallInputs(in, layout); !err.isEmpty()) return {false, err};
    QDir(layout.moduleDir()).removeRecursively(); // a half-finished earlier build must not look built
    QDir(layout.exportDir()).removeRecursively(); // always regenerated (about 2 s): never mix two exports
    QDir().mkpath(layout.exportDir());

    report(10, QCoreApplication::translate("Messages", "Reading the game"));
    {
        QProcess ex;
        ex.setProcessChannelMode(QProcess::SeparateChannels);
        BuildRunner::DieWithParent(ex);
        QStringList args{"--base", layout.base(), "--update", layout.update(), "--keys", layout.keysFile(), "--out", layout.exportDir(), "--progress-fd", "1"};
        if (in.decrypted()) args = {"--exefs", layout.decryptedExefs(), "--out", layout.exportDir(), "--progress-fd", "1"};
        for (const QString& p : in.patches) args << "--patch" << p;
        if (BuildRunner::FastCode()) args << "--native-calls" << "--chain-cond"; // --fast-entry (two functions per block) measured no gain and 9% bigger modules: off
        ex.start(paths.exporter, args);
        if (!ex.waitForStarted()) return {false, QCoreApplication::translate("Messages", "Cannot start the exporter (%1).").arg(paths.exporter)};
        while (!ex.waitForFinished(200)) {
            if (Cancelled(cancel)) { ex.kill(); ex.waitForFinished(); return {false, QStringLiteral("Cancelled.")}; }
            while (ex.canReadLine()) {
                const auto parts = ex.readLine().trimmed().split(' ');
                if (parts.value(0) == "PROGRESS") report(10 + parts.value(1).toInt() * 90 / 100, QString::fromUtf8(parts.mid(2).join(' ')));
            }
        }
        if (ex.exitCode() != 0) {
            const QString err = QString::fromLocal8Bit(ex.readAllStandardError()).trimmed();
            return {false, QCoreApplication::translate("Messages", "Reading the game failed: %1").arg(err.isEmpty() ? QCoreApplication::translate("Messages", "exit code %1").arg(ex.exitCode()) : err)};
        }
    }

    BuildPlan plan;
    plan.useToolchain(paths.toolchain);
    plan.exportDir = layout.exportDir();
    plan.workDir = layout.workDir();
    plan.outDir = layout.moduleDir();
    plan.hotList = hotList;
    plan.jobs = jobs;
    {
        QFile list(QDir(layout.exportDir()).filePath("code_patches.txt"));
        if (list.open(QIODevice::ReadOnly)) plan.codePatches = list.readAll();
    }
    plan.stamp = paths.engineVersion + "|" + FileText(paths.toolchain + "/TOOLCHAIN_VERSION") + "|" + FileHash(hotList) + "|" + updateName + (BuildRunner::FastCode() ? "|hot-O3-fast1" : "|hot-O3");
    const auto built = BuildRunner::Build(plan, [&](int done, int total, const QString& what) {
        report(100 + 850 * done / std::max(total, 1), QCoreApplication::translate("Messages", "Compiling %1 of %2: %3").arg(done).arg(total).arg(what));
    }, cancel);
    if (!built.ok) return {false, built.message};

    // The engine makes the same changes to the loaded code (the code guard compares it with the translation).
    QFile::remove(QDir(layout.moduleDir()).filePath("code_patches.txt"));
    if (!plan.codePatches.isEmpty() && !QFile::copy(QDir(layout.exportDir()).filePath("code_patches.txt"), QDir(layout.moduleDir()).filePath("code_patches.txt")))
        return {false, QCoreApplication::translate("Messages", "Cannot write the code patch list into %1.").arg(layout.moduleDir())};

    report(950, QCoreApplication::translate("Messages", "Testing the game"));
    const VerifyResult verdict = VerifyBuild(paths.engine, layout, 150, cancel);
    if (!verdict.ok) return {false, verdict.message};

    QFile stamp(layout.moduleDir() + "/module.json");
    if (!stamp.open(QIODevice::WriteOnly)) return {false, QCoreApplication::translate("Messages", "Cannot write %1.").arg(stamp.fileName())};
    stamp.write("{ \"engineVersion\": \"" + paths.engineVersion.toUtf8() + "\", \"update\": \"" + updateName.toUtf8() + "\" }\n");
    report(1000, QCoreApplication::translate("Messages", "The game is built"));
    return {true, {}};
}
