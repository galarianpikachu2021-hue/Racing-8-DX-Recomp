// SPDX-License-Identifier: GPL-3.0-or-later
#include "build_runner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#endif
#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "unit_split.h"

namespace {
constexpr qint64 kGiB = 1024LL * 1024 * 1024;

QStringList CommonArgs(bool hot, const QString& includeDir) {
    QStringList a = {"-std=gnu11", "-O2", "-foptimize-sibling-calls", "-ffunction-sections", "-march=x86-64-v3", "-Wno-error",
                     "-ffp-contract=off", "-fno-fast-math", "-fvisibility=hidden", "-DNDEBUG", "-DSUYU_HOSTED_RECOMP=1",
                     "-DRECOMP_SHARED_MODULE=1", "-I" + includeDir};
#ifndef Q_OS_WIN
    a << "-fPIC"; // Windows DLLs are position-dependent by design (and the mingw driver rejects the flag)
#endif
    // Hot pieces at -O3: measured 2% less game-thread CPU than -O2 (4.0.1, 4 races each), same compile time and size. Cold pieces
    // are compiled cheaply.
    a << (hot ? "-O3" : "-O0");
    return a;
}
} // namespace

QString BuildRunner::ModuleFileName(const QString& name) {
#ifdef Q_OS_WIN
    return QStringLiteral("recompiled_%1.dll").arg(name);
#else
    return QStringLiteral("recompiled_%1.so").arg(name);
#endif
}

qint64 BuildRunner::AvailableMemoryBytes() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) return static_cast<qint64>(m.ullAvailPhys);
    return 8LL << 30;
#else
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly)) return 8LL << 30;
    for (const QByteArray& line : f.readAll().split('\n'))
        if (line.startsWith("MemAvailable:")) return line.simplified().split(' ').value(1).toLongLong() * 1024;
    return 8LL << 30;
#endif
}

int BuildRunner::JobsForRam(qint64 availableBytes, int threads) {
    if (availableBytes < 3 * kGiB) return 0;
    // Every logical thread counts (hyper-threads included), but 15% stay free so the PC remains usable while it builds.
    const qint64 cpuJobs = std::max<qint64>(1, static_cast<qint64>(threads) * 85 / 100);
    return static_cast<int>(std::max<qint64>(1, std::min<qint64>(cpuJobs, availableBytes / (2 * kGiB))));
}

bool BuildRunner::LinkModule(const QString& compiler, const QStringList& linkArgs, const QStringList& libs, const QStringList& builtins,
                             const QStringList& objects, const QString& out, const QString& rspPath, QString* error,
                             const std::atomic<bool>* cancel) {
    QDir().mkpath(QFileInfo(rspPath).absolutePath());
    QFile rsp(rspPath);
    if (!rsp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(rspPath);
        return false;
    }
    for (const QString& o : objects) rsp.write("\"" + QString(o).replace('\\', '/').toUtf8() + "\"\n");
    rsp.close();
    QProcess link;
    DieWithParent(link);
    link.start(compiler, QStringList{"-shared"} + linkArgs + QStringList{"-o", out, "@" + rspPath} + libs + builtins);
    if (!link.waitForStarted()) {
        if (error) *error = QStringLiteral("cannot start %1: %2").arg(compiler, link.errorString());
        return false;
    }
    while (!link.waitForFinished(200)) {
        if (link.state() == QProcess::NotRunning) break; // never wait on a process that is already gone
        if (cancel && cancel->load()) { link.kill(); link.waitForFinished(); if (error) *error = QStringLiteral("Cancelled."); return false; }
    }
    if (link.exitStatus() != QProcess::NormalExit || link.exitCode() != 0) {
        if (error) *error = QString::fromLocal8Bit(link.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}

void BuildRunner::OrderForScheduling(QList<const CompileJob*>& jobs) {
    QHash<const CompileJob*, qint64> size;
    for (const CompileJob* j : jobs) size.insert(j, QFileInfo(j->source).size());
    std::stable_sort(jobs.begin(), jobs.end(), [&](const CompileJob* a, const CompileJob* b) {
        if (a->hot != b->hot) return a->hot;
        return size.value(a) > size.value(b);
    });
}

bool BuildRunner::HasDiskSpace(const QString& dir, qint64 neededBytes) {
    // The folder may not exist yet (first run): measure the closest parent that does.
    QString probe = QDir::cleanPath(QDir(dir).absolutePath());
    while (!QFileInfo::exists(probe)) {
        const QString parent = QFileInfo(probe).absolutePath();
        if (parent == probe) break;
        probe = parent;
    }
    return QStorageInfo(probe).bytesAvailable() >= neededBytes;
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

void BuildRunner::DieWithParent(QProcess& process) {
#ifdef Q_OS_LINUX
    // A child must never outlive the launcher (a closed window, a crash): the kernel kills it when its parent thread ends.
    process.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
#elif defined(Q_OS_WIN)
    // One job object for the whole launcher; Windows kills everything in it when the launcher's last handle to it closes (exit or crash).
    static HANDLE job = [] {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (h) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return h;
    }();
    if (!job) return;
    QObject::connect(&process, &QProcess::started, &process, [&process] {
        if (HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(process.processId()))) {
            AssignProcessToJobObject(job, h);
            CloseHandle(h);
        }
    });
#else
    Q_UNUSED(process)
#endif
}

bool BuildRunner::RunCompileJob(const QString& compiler, const CompileJob& job, QString* error, const std::atomic<bool>* cancel) {
    QFile::remove(job.marker);
    QFile::remove(job.object);
    QDir().mkpath(QFileInfo(job.object).absolutePath());
    QProcess p;
    p.setProcessChannelMode(QProcess::SeparateChannels);
    DieWithParent(p);
    p.start(compiler, job.args + QStringList{"-c", job.source, "-o", job.object});
    if (!p.waitForStarted()) {
        if (error) *error = QStringLiteral("cannot start %1").arg(compiler);
        return false;
    }
    while (!p.waitForFinished(200)) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished();
            QFile::remove(job.object);
            if (error) *error = QStringLiteral("Cancelled.");
            return false;
        }
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        QFile::remove(job.object);
        if (error) *error = QStringLiteral("The compiler was stopped, most likely because the PC ran out of memory. Close other programs and press Build again: it resumes where it stopped.");
        return false;
    }
    if (p.exitCode() != 0 || !QFile::exists(job.object)) {
        const QString text = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        if (error) *error = text.contains(QStringLiteral("No space left")) ? QStringLiteral("Out of disk space. Free some space and press Build again: it resumes where it stopped.") : text;
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
#ifdef Q_OS_WIN
    // llvm-mingw: the target's headers, CRT and import libraries sit under the toolchain folder, and clang finds them itself.
    compiler = QDir(t).filePath("bin/clang.exe");
    compileArgs = {"--target=x86_64-w64-mingw32"};
    builtins = {};
    linkArgs = {"--target=x86_64-w64-mingw32", "-fuse-ld=lld", "-rtlib=compiler-rt", "-unwindlib=none"}; // the generated sources mark their entry points __declspec(dllexport)
    linkLibs = {"-lm"};
#else
    compiler = QDir(t).filePath("bin/clang");
    compileArgs = {"--target=x86_64-linux-gnu", "--sysroot=" + QDir(t).filePath("sysroot")};
    builtins = {QDir(t).filePath("lib/libclang_rt.builtins-x86_64.a")};
    linkArgs = {"--target=x86_64-linux-gnu", "--sysroot=" + QDir(t).filePath("sysroot"), "-fuse-ld=lld", "-nostdlib",
                "-L" + QDir(t).filePath("sysroot/lib/x86_64-linux-gnu"), "-L" + QDir(t).filePath("sysroot/usr/lib/x86_64-linux-gnu")};
#endif
}

BuildResult BuildRunner::Build(const BuildPlan& plan, const std::function<void(int, int, const QString&)>& progress,
                               const std::atomic<bool>* cancel) {
    // What the work folder holds is only valid for the engine, toolchain and hot list it was made with.
    if (!plan.stamp.isEmpty()) {
        const QString stampFile = QDir(plan.workDir).filePath("STAMP");
        QFile old(stampFile);
        const bool same = old.open(QIODevice::ReadOnly) && old.readAll().trimmed() == plan.stamp.toUtf8();
        old.close();
        if (!same) QDir(plan.workDir).removeRecursively();
        QDir().mkpath(plan.workDir);
        QFile fresh(stampFile);
        if (fresh.open(QIODevice::WriteOnly)) fresh.write(plan.stamp.toUtf8() + "\n");
    }
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
    OrderForScheduling(pending);
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
            const bool ok = RunCompileJob(plan.compiler, job, &error, cancel);
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
    // The modules link side by side (the big main module no longer waits behind the small ones), and the linker gets the same thread
    // budget as the compiles instead of its own default of every thread.
    const QString linkThreads = QStringLiteral("-Wl,--threads=%1").arg(std::max(1, plan.jobs));
    std::vector<std::thread> linkers;
    QString linkFailure;
    int linked = 0;
    for (const Module& m : modules) {
        linkers.emplace_back([&, mp = &m] {
            QStringList objects;
            for (const CompileJob& j : mp->jobs) objects << j.object;
            const QString out = QDir(plan.outDir).filePath(ModuleFileName(mp->name));
            QString error;
            const bool ok = LinkModule(plan.compiler, plan.linkArgs + QStringList{linkThreads}, plan.linkLibs, plan.builtins, objects, out,
                                       QDir(plan.workDir).filePath(mp->name + ".link.rsp"), &error, cancel);
            std::lock_guard<std::mutex> lock(mutex);
            if (!ok) {
                if (linkFailure.isEmpty()) linkFailure = QStringLiteral("linking %1 failed: %2").arg(out, error);
                return;
            }
            ++linked;
            if (progress) progress(total, total, QStringLiteral("linked %1").arg(QFileInfo(out).fileName()));
        });
    }
    for (auto& t : linkers) t.join();
    if (cancel && cancel->load()) return {false, QStringLiteral("Cancelled.")};
    if (!linkFailure.isEmpty()) return {false, linkFailure};
    if (linked != modules.size()) return {false, QStringLiteral("linking was interrupted")};
    return {true, {}};
}
