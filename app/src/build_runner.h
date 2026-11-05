// SPDX-License-Identifier: GPL-3.0-or-later
// Compiles an export's generated C into the game's module libraries (recompiled_<name>.so) with a bundled clang: splits the units
// (UnitSplit), plans one compile job per piece, runs them in parallel sized by free RAM, resumes after an interruption (a piece counts
// as finished only when its object file AND its .done marker exist) and links the modules. Blocking: run Build() on a worker thread.
#pragma once

#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

struct CompileJob {
    QString source;
    QString object;
    QString marker; // <object>.done, written only after the object was produced successfully
    QStringList args;
    bool hot;
};

struct BuildPlan {
    QString exportDir; // kartrecomp-export output: <exportDir>/{rtld,main,subsdk0,sdk}/{src,recomp_export.c,recomp_runtime.c}
    QString workDir;   // split pieces and objects, kept between runs for resuming
    QString outDir;    // recompiled_<name>.so
    QString compiler;  // clang
    QString hotList;   // file with one hot block function name per line (may be empty: everything compiled cold)
    QStringList compileArgs; // added to every compile, e.g. --target=x86_64-linux-gnu --sysroot=<toolchain>/sysroot
    QStringList builtins;    // compiler-rt builtins archive(s) linked last, e.g. <toolchain>/lib/libclang_rt.builtins-x86_64.a
    QStringList linkArgs;    // added to the module link, e.g. -fuse-ld=lld -nostdlib -L... (before the objects) and the libraries
    /// Fills compiler, compileArgs and linkArgs from a bundled toolchain folder (packaging/make-toolchain.sh).
    void useToolchain(const QString& toolchainDir);
    QStringList linkLibs = {"-lm", "-lc"}; // after the objects; Windows (mingw) has no libc to name
    QString stamp;   // identifies what the work folder was built with (engine, toolchain, hot list); a different stamp wipes it
    int jobs = 1;
};

struct BuildResult {
    bool ok = false;
    QString message;
};

class BuildRunner {
public:
    /// Concurrent compiles that fit: 2 GB per job, at least 1, at most `threads`; 0 when under 3 GB is free (refuse).
    /// recompiled_<name>.so on Linux, recompiled_<name>.dll on Windows: what the engine looks for in the module folder.
    static QString ModuleFileName(const QString& name);
    /// Free RAM the build can use (MemAvailable on Linux, GlobalMemoryStatusEx on Windows); 8 GiB if the system will not say.
    static qint64 AvailableMemoryBytes();
    static int JobsForRam(qint64 availableBytes, int threads);
    /// Hot pieces first, then the biggest source first, so the longest compiles never start last and leave one core busy at the end.
    static void OrderForScheduling(QList<const CompileJob*>& jobs);
    static bool HasDiskSpace(const QString& dir, qint64 neededBytes);
    /// One job per file in `files` (names inside srcDir). `hotFiles` get full optimisation, the rest -O0.
    /// `includeDir` defaults to the folder above srcDir (where recomp_runtime.h lives).
    static QList<CompileJob> PlanCompileJobs(const QString& srcDir, const QString& objDir, const QStringList& files,
                                             const QStringList& hotFiles, const QString& includeDir = {});
    static bool IsDone(const CompileJob& job);
    /// Makes a child die with this process (Linux: PR_SET_PDEATHSIG); call before start().
    static void DieWithParent(QProcess& process);
    static bool RunCompileJob(const QString& compiler, const CompileJob& job, QString* error, const std::atomic<bool>* cancel = nullptr);

    /// Whole build. `progress(done, total, what)` is called from worker threads after every finished job (serialised).
    static BuildResult Build(const BuildPlan& plan, const std::function<void(int, int, const QString&)>& progress,
                             const std::atomic<bool>* cancel = nullptr);
};
