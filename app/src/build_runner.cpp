// SPDX-License-Identifier: GPL-3.0-or-later
#include "build_runner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <algorithm>
#include <mutex>
#include <thread>

#include "unit_split.h"

namespace {
constexpr qint64 kGiB = 1024LL * 1024 * 1024;

QStringList CommonArgs(bool hot, const QString& includeDir) {
    QStringList a = {"-std=gnu11", "-O2", "-foptimize-sibling-calls", "-ffunction-sections", "-march=x86-64-v3", "-Wno-error",
                     "-ffp-contract=off", "-fno-fast-math", "-fPIC", "-fvisibility=hidden", "-DNDEBUG", "-DSUYU_HOSTED_RECOMP=1",
                     "-DRECOMP_SHARED_MODULE=1", "-I" + includeDir};
    if (!hot) a << "-O0"; // after -O2: the cold pieces are compiled cheaply
    return a;
}
} // namespace

int BuildRunner::JobsForRam(qint64 availableBytes, int threads) {
    if (availableBytes < 3 * kGiB) return 0;
    return static_cast<int>(std::max<qint64>(1, std::min<qint64>(threads, availableBytes / (2 * kGiB))));
}

bool BuildRunner::HasDiskSpace(const QString& dir, qint64 neededBytes) {
    return QStorageInfo(dir).bytesAvailable() >= neededBytes;
}

QList<CompileJob> BuildRunner::PlanCompileJobs(const QString& srcDir, const QString& objDir, const QStringList& files,
                                               const QStringList& hotFiles, const QString& includeDir) {
    const QString include = includeDir.isEmpty() ? QDir::cleanPath(srcDir + "/..") : includeDir;
    QList<CompileJob> jobs;
    for (const QString& f : files) {
        const bool hot = hotFiles.contains(f);
        const QString object = QDir(objDir).filePath(f + ".o");
        jobs.append({QDir(srcDir).filePath(f), object, object + ".done", CommonArgs(hot, include), hot});
    }
    return jobs;
}

bool BuildRunner::IsDone(const CompileJob& job) {
    return QFile::exists(job.object) && QFile::exists(job.marker);
}

bool BuildRunner::RunCompileJob(const QString& compiler, const CompileJob& job, QString* error) {
    QFile::remove(job.marker);
    QFile::remove(job.object);
    QDir().mkpath(QFileInfo(job.object).absolutePath());
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(compiler, job.args + QStringList{"-c", job.source, "-o", job.object});
    if (!p.waitForStarted()) {
        if (error) *error = QStringLiteral("cannot start %1").arg(compiler);
        return false;
    }
    p.waitForFinished(-1);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0 || !QFile::exists(job.object)) {
        if (error) *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        QFile::remove(job.object);
        return false;
    }
    QFile marker(job.marker);
    if (!marker.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(job.marker);
        return false;
    }
    return true;
}

void BuildPlan::useToolchain(const QString& t) {
    compiler = QDir(t).filePath("bin/clang");
    compileArgs = {"--target=x86_64-linux-gnu", "--sysroot=" + QDir(t).filePath("sysroot")};
    builtins = {QDir(t).filePath("lib/libclang_rt.builtins-x86_64.a")};
    linkArgs = {"--target=x86_64-linux-gnu", "--sysroot=" + QDir(t).filePath("sysroot"), "-fuse-ld=lld", "-nostdlib",
                "-L" + QDir(t).filePath("sysroot/lib/x86_64-linux-gnu"), "-L" + QDir(t).filePath("sysroot/usr/lib/x86_64-linux-gnu")};
}

BuildResult BuildRunner::Build(const BuildPlan& plan, const std::function<void(int, int, const QString&)>& progress,
                               const std::atomic<bool>* cancel) {
    QSet<QByteArray> hot;
    if (!plan.hotList.isEmpty()) {
        QFile f(plan.hotList);
        if (!f.open(QIODevice::ReadOnly)) return {false, QStringLiteral("cannot read the hot block list %1").arg(plan.hotList)};
        hot = UnitSplit::ParseHotList(f.readAll());
    }
    static const QStringList kModuleDirs = {"rtld", "main", "subsdk0", "subsdk1", "subsdk2", "subsdk3", "subsdk4",
                                            "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9", "sdk"};
    struct Module { QString dir, name; QList<CompileJob> jobs; };
    QList<Module> modules;
    for (const QString& dir : kModuleDirs) {
        const QString mdir = QDir(plan.exportDir).filePath(dir);
        if (!QFile::exists(QDir(mdir).filePath("recomp_export.c"))) continue;
        Module m{dir, dir == "main" ? "image" : dir, {}};
        const QString work = QDir(plan.workDir).filePath(dir);
        QStringList pieces, hotPieces;
        QDirIterator it(QDir(mdir).filePath("src"), {QStringLiteral("recompiled_%1_*.c").arg(dir)}, QDir::Files);
        QStringList units;
        while (it.hasNext()) units << it.next();
        units.sort();
        for (const QString& unit : units) {
            QString error;
            const auto split = UnitSplit::Split(unit, work + "/src", hot, 4, &error);
            if (split.isEmpty()) return {false, error};
            for (const auto& p : split) {
                pieces << p.name;
                if (p.hot) hotPieces << p.name;
            }
        }
        m.jobs = PlanCompileJobs(work + "/src", work + "/obj", pieces, hotPieces, mdir);
        m.jobs += PlanCompileJobs(mdir, work + "/obj", {"recomp_export.c", "recomp_runtime.c"}, {"recomp_export.c", "recomp_runtime.c"}, mdir);
        // The module's index file (src/recompiled_<dir>.c: the block lookup, _recomp_index_view) sits beside the numbered units.
        const QString index = QStringLiteral("recompiled_%1.c").arg(dir);
        if (QFile::exists(QDir(mdir).filePath("src/" + index)))
            m.jobs += PlanCompileJobs(QDir(mdir).filePath("src"), work + "/obj-index", {index}, {index}, mdir);
        modules.append(std::move(m));
    }
    if (modules.isEmpty()) return {false, QStringLiteral("%1 holds no exported modules").arg(plan.exportDir)};

    for (Module& m : modules)
        for (CompileJob& j : m.jobs) j.args = plan.compileArgs + j.args;
    QList<const CompileJob*> pending;
    int total = 0, done = 0;
    for (const Module& m : modules) {
        for (const CompileJob& j : m.jobs) {
            ++total;
            if (IsDone(j)) ++done;
            else pending.append(&j);
        }
    }
    // The big pieces first, so the long compiles do not start last.
    std::stable_sort(pending.begin(), pending.end(), [](const CompileJob* a, const CompileJob* b) { return a->hot && !b->hot; });
    std::mutex mutex;
    std::atomic<qsizetype> next{0};
    QString failure;
    const auto worker = [&] {
        for (;;) {
            if (cancel && cancel->load()) return;
            const qsizetype i = next.fetch_add(1);
            if (i >= pending.size()) return;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!failure.isEmpty()) return;
            }
            QString error;
            const CompileJob& job = *pending[i];
            const bool ok = RunCompileJob(plan.compiler, job, &error);
            std::lock_guard<std::mutex> lock(mutex);
            if (!ok) {
                if (failure.isEmpty()) failure = QStringLiteral("compiling %1 failed: %2").arg(QFileInfo(job.source).fileName(), error);
                return;
            }
            ++done;
            if (progress) progress(done, total, QFileInfo(job.source).fileName());
        }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < std::max(1, plan.jobs); ++t) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    if (cancel && cancel->load()) return {false, QStringLiteral("cancelled")};
    if (!failure.isEmpty()) return {false, failure};

    QDir().mkpath(plan.outDir);
    for (const Module& m : modules) {
        QStringList objects;
        for (const CompileJob& j : m.jobs) objects << j.object;
        const QString out = QDir(plan.outDir).filePath(QStringLiteral("recompiled_%1.so").arg(m.name));
        QProcess link;
        link.start(plan.compiler, QStringList{"-shared"} + plan.linkArgs + QStringList{"-o", out} + objects +
                       QStringList{"-lm", "-lc"} + plan.builtins);
        link.waitForFinished(-1);
        if (link.exitStatus() != QProcess::NormalExit || link.exitCode() != 0)
            return {false, QStringLiteral("linking %1 failed: %2").arg(out, QString::fromLocal8Bit(link.readAllStandardError()).trimmed())};
        if (progress) progress(total, total, QStringLiteral("linked %1").arg(QFileInfo(out).fileName()));
    }
    return {true, {}};
}
