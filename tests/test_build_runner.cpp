// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "test_support.h"
#include <thread>

#include "build_runner.h"

class TestBuildRunner : public QObject {
    Q_OBJECT
private slots:
    void modulesNameFollowThePlatform() {
#ifdef Q_OS_WIN
        QCOMPARE(BuildRunner::ModuleFileName("image"), QString("recompiled_image.dll"));
#else
        QCOMPARE(BuildRunner::ModuleFileName("image"), QString("recompiled_image.so"));
#endif
    }
    void jobsFollowFreeRamAtTwoGigabytesEach() {
        constexpr qint64 GiB = 1024LL * 1024 * 1024;
        QCOMPARE(BuildRunner::JobsForRam(64 * GiB, 32), 27);
        QCOMPARE(BuildRunner::JobsForRam(8 * GiB, 32), 4);
        QCOMPARE(BuildRunner::JobsForRam(3 * GiB, 8), 1);
    }
    void jobsUseAtMostEightyFivePercentOfTheLogicalThreads() {
        constexpr qint64 GiB = 1024LL * 1024 * 1024;
        QCOMPARE(BuildRunner::JobsForRam(64 * GiB, 24), 20); // hyper-threads count: 24 logical threads -> 20 jobs, not 12
        QCOMPARE(BuildRunner::JobsForRam(64 * GiB, 8), 6);
        QCOMPARE(BuildRunner::JobsForRam(64 * GiB, 4), 3);
        QCOMPARE(BuildRunner::JobsForRam(64 * GiB, 2), 1);
        QCOMPARE(BuildRunner::JobsForRam(64 * GiB, 1), 1);
    }
    void compileJobsAreOrderedHotFirstThenBiggestFirst() {
        QTemporaryDir dir;
        const auto make = [&](const char* name, int bytes) {
            QFile f(dir.filePath(name));
            if (f.open(QIODevice::WriteOnly)) f.write(QByteArray(bytes, 'x'));
            f.close();
            return CompileJob{f.fileName(), {}, {}, {}, false};
        };
        CompileJob small = make("small.c", 10), big = make("big.c", 5000), hotSmall = make("hot_small.c", 20);
        hotSmall.hot = true;
        QList<const CompileJob*> jobs{&small, &hotSmall, &big};
        BuildRunner::OrderForScheduling(jobs);
        QCOMPARE(jobs, (QList<const CompileJob*>{&hotSmall, &big, &small}));
    }
    void refusesBelowThreeGigabytesFree() {
        constexpr qint64 GiB = 1024LL * 1024 * 1024;
        QCOMPARE(BuildRunner::JobsForRam(2 * GiB + GiB / 2, 8), 0);
    }
    void diskSpaceCheckFailsForAnImpossibleRequest() {
        QTemporaryDir dir;
        QVERIFY(BuildRunner::HasDiskSpace(dir.path(), 1024));
        QVERIFY(!BuildRunner::HasDiskSpace(dir.path(), 10LL * 1024 * 1024 * 1024 * 1024));
    }
    void diskSpaceIsMeasuredOnTheClosestExistingParentOfAFolderNotYetCreated() {
        QTemporaryDir dir;
        QVERIFY(BuildRunner::HasDiskSpace(dir.filePath("not/yet/created"), 1024));
        QVERIFY(!BuildRunner::HasDiskSpace(dir.filePath("not/yet/created"), 10LL * 1024 * 1024 * 1024 * 1024));
    }
    void aChangedBuildStampWipesTheWorkFolderAndTheSameStampKeepsIt() {
        NEEDS_SH();
        QTemporaryDir dir;
        const auto touch = [&](const QString& rel, const QByteArray& text = {}) {
            QDir().mkpath(QFileInfo(dir.filePath(rel)).absolutePath());
            QFile f(dir.filePath(rel));
            f.open(QIODevice::WriteOnly);
            f.write(text);
        };
        touch("export/main/recomp_export.c");
        touch("export/main/recomp_runtime.c");
        touch("export/main/src/recompiled_main.c");
        touch("export/main/src/recompiled_main_0.c", "static uint32_t recomp_gg_seen[4];\nvoid blk_main_0000000000000000(GuestContext* c){\n}\nconst struct _recomp_ent _seg_main_0[] = {\n};\n");
        touch("fakecc", "#!/bin/sh\nout=; while [ $# -gt 0 ]; do [ \"$1\" = -o ] && out=\"$2\"; shift; done\necho o > \"$out\"\n");
        QFile::setPermissions(dir.filePath("fakecc"), QFile::permissions(dir.filePath("fakecc")) | QFileDevice::ExeOwner);
        BuildPlan plan;
        plan.exportDir = dir.filePath("export");
        plan.workDir = dir.filePath("work");
        plan.outDir = dir.filePath("out");
        plan.compiler = dir.filePath("fakecc");
        plan.stamp = "engine-1";
        QVERIFY(BuildRunner::Build(plan, nullptr).ok);
        touch("work/main/obj/old-object.o"); // something left by an earlier build
        QVERIFY(BuildRunner::Build(plan, nullptr).ok);
        QVERIFY(QFile::exists(dir.filePath("work/main/obj/old-object.o"))); // same stamp: resumed, not wiped
        plan.stamp = "engine-2"; // a new engine, toolchain or hot list
        QVERIFY(BuildRunner::Build(plan, nullptr).ok);
        QVERIFY(!QFile::exists(dir.filePath("work/main/obj/old-object.o"))); // different stamp: everything compiled again
    }
    void cancelStopsTheRunningCompilerPromptly() {
        NEEDS_SH();
        QTemporaryDir dir;
        const auto touch = [&](const QString& rel, const QByteArray& text = {}) {
            QDir().mkpath(QFileInfo(dir.filePath(rel)).absolutePath());
            QFile f(dir.filePath(rel));
            f.open(QIODevice::WriteOnly);
            f.write(text);
        };
        touch("export/main/recomp_export.c");
        touch("export/main/recomp_runtime.c");
        touch("export/main/src/recompiled_main.c");
        touch("export/main/src/recompiled_main_0.c", "static uint32_t recomp_gg_seen[4];\nvoid blk_main_0000000000000000(GuestContext* c){\n}\nconst struct _recomp_ent _seg_main_0[] = {\n};\n");
        touch("fakecc", ("#!/bin/sh\necho $$ >> " + dir.filePath("pids") + "\nexec sleep 30\n").toLocal8Bit());
        QFile::setPermissions(dir.filePath("fakecc"), QFile::permissions(dir.filePath("fakecc")) | QFileDevice::ExeOwner);
        BuildPlan plan;
        plan.exportDir = dir.filePath("export");
        plan.workDir = dir.filePath("work");
        plan.outDir = dir.filePath("out");
        plan.compiler = dir.filePath("fakecc");
        plan.jobs = 2;
        std::atomic<bool> cancel{false};
        std::thread stopper([&] { QThread::msleep(600); cancel = true; });
        QElapsedTimer t;
        t.start();
        const auto r = BuildRunner::Build(plan, nullptr, &cancel);
        stopper.join();
        QVERIFY2(t.elapsed() < 6000, "cancel did not stop the compilers");
        QVERIFY(!r.ok);
        QVERIFY(r.message.contains("ancel"));
        QFile pids(dir.filePath("pids"));
        QVERIFY(pids.open(QIODevice::ReadOnly));
        for (const QByteArray& pid : pids.readAll().split('\n'))
            if (!pid.trimmed().isEmpty()) QVERIFY2(!QFileInfo::exists("/proc/" + pid.trimmed()), "a compiler is still running after cancel");
    }
    void aCompilerKilledBySignalIsReportedAsOutOfMemory() {
        NEEDS_SH();
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("src");
        QFile(dir.filePath("src/a.c")).open(QIODevice::WriteOnly);
        QFile fake(dir.filePath("fakecc"));
        fake.open(QIODevice::WriteOnly);
        fake.write("#!/bin/sh\nkill -9 $$\n");
        fake.close();
        fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner);
        auto jobs = BuildRunner::PlanCompileJobs(dir.filePath("src"), dir.filePath("obj"), {"a.c"}, {});
        QString err;
        QVERIFY(!BuildRunner::RunCompileJob(dir.filePath("fakecc"), jobs[0], &err));
        QVERIFY2(err.contains("memory"), qPrintable(err));
    }
    void aPieceWithAnObjectButNoMarkerIsPlannedAgain() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("src");
        QDir(dir.path()).mkpath("obj");
        for (const char* n : {"a_p0.c", "b_p0.c", "b_hot.c"}) QFile(dir.filePath(QString("src/") + n)).open(QIODevice::WriteOnly);
        // a_p0 finished (object + marker), b_p0 has an object but no marker (interrupted), b_hot has nothing
        for (const char* f : {"a_p0.c.o", "a_p0.c.o.done", "b_p0.c.o"}) QFile(dir.filePath(QString("obj/") + f)).open(QIODevice::WriteOnly);
        const auto jobs = BuildRunner::PlanCompileJobs(dir.filePath("src"), dir.filePath("obj"), {"a_p0.c", "b_p0.c", "b_hot.c"}, {"b_hot.c"});
        QCOMPARE(jobs.size(), 3);
        QVERIFY(BuildRunner::IsDone(jobs[0]));
        QVERIFY(!BuildRunner::IsDone(jobs[1]));
        QVERIFY(!BuildRunner::IsDone(jobs[2]));
        QVERIFY(jobs[2].hot && !jobs[1].hot);
        QVERIFY(jobs[1].args.contains("-O0") && !jobs[2].args.contains("-O0"));
        QVERIFY(jobs[2].args.contains("-O2") && jobs[2].args.contains("-march=x86-64-v3"));
    }
    void aFinishedCompileWritesTheMarkerAndAFailedOneDoesNot() {
        NEEDS_SH();
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("src");
        QDir(dir.path()).mkpath("obj");
        QFile(dir.filePath("src/ok.c")).open(QIODevice::WriteOnly);
        QFile(dir.filePath("src/bad.c")).open(QIODevice::WriteOnly);
        // a fake compiler: writes the -o file unless the source is bad.c
        QFile fake(dir.filePath("fakecc"));
        fake.open(QIODevice::WriteOnly);
        fake.write("#!/bin/sh\nout=; src=; while [ $# -gt 0 ]; do case \"$1\" in -o) out=$2; shift;; *.c) src=$1;; esac; shift; done\n"
                   "case \"$src\" in *bad.c) echo boom >&2; exit 1;; esac; echo obj > \"$out\"\n");
        fake.close();
        fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner);
        auto jobs = BuildRunner::PlanCompileJobs(dir.filePath("src"), dir.filePath("obj"), {"ok.c", "bad.c"}, {});
        QVERIFY(BuildRunner::RunCompileJob(dir.filePath("fakecc"), jobs[0], nullptr));
        QVERIFY(BuildRunner::IsDone(jobs[0]));
        QString err;
        QVERIFY(!BuildRunner::RunCompileJob(dir.filePath("fakecc"), jobs[1], &err));
        QVERIFY(!BuildRunner::IsDone(jobs[1]));
        QVERIFY(err.contains("boom"));
    }
    void aBuildCompilesEveryModuleSourceIncludingTheIndexFile() {
        NEEDS_SH();
        QTemporaryDir dir;
        const auto touch = [&](const QString& rel, const QByteArray& text = {}) {
            QDir().mkpath(QFileInfo(dir.filePath(rel)).absolutePath());
            QFile f(dir.filePath(rel));
            f.open(QIODevice::WriteOnly);
            f.write(text);
        };
        // a minimal export: module rtld with a runtime, an export file, the module's index file and one generated unit
        touch("export/rtld/recomp_export.c");
        touch("export/rtld/recomp_runtime.c");
        touch("export/rtld/src/recompiled_rtld.c");
        touch("export/rtld/src/recompiled_rtld_0.c",
              "static uint32_t recomp_gg_seen[4];\nvoid blk_rtld_0000000000000000(GuestContext* c){\n}\n"
              "const struct _recomp_ent _seg_rtld_0[] = {\n};\n");
        QFile fake(dir.filePath("fakecc"));
        fake.open(QIODevice::WriteOnly);
        fake.write(("#!/bin/sh\necho \"$@\" >> " + dir.filePath("calls.log") + "\nout=; while [ $# -gt 0 ]; do case \"$1\" in -o) out=$2; shift;; esac; shift; done\necho obj > \"$out\"\n").toLocal8Bit());
        fake.close();
        fake.setPermissions(fake.permissions() | QFileDevice::ExeOwner);
        BuildPlan plan;
        plan.exportDir = dir.filePath("export");
        plan.workDir = dir.filePath("work");
        plan.outDir = dir.filePath("out");
        plan.compiler = dir.filePath("fakecc");
        plan.compileArgs = {"--sysroot=/toolchain/sysroot"};
        plan.linkArgs = {"-nostdlib", "-lm"};
        plan.jobs = 2;
        const auto result = BuildRunner::Build(plan, nullptr);
        QVERIFY2(result.ok, qPrintable(result.message));
        QFile log(dir.filePath("calls.log"));
        log.open(QIODevice::ReadOnly);
        const QByteArray calls = log.readAll();
        QVERIFY(calls.contains("recompiled_rtld.c"));          // the index file (defines _recomp_index_view)
        QVERIFY(calls.contains("recompiled_rtld_0_p0.c"));     // a split piece
        QVERIFY(calls.contains("recomp_runtime.c") && calls.contains("recomp_export.c"));
        QVERIFY(QFile::exists(dir.filePath("out/" + BuildRunner::ModuleFileName("rtld"))));
        // compile args go to every compile, link args only to the link
        for (const QByteArray& line : calls.split('\n')) {
            if (line.contains(" -c ")) QVERIFY2(line.contains("--sysroot=/toolchain/sysroot") && !line.contains("-nostdlib"), line.constData());
            if (line.contains("-shared")) QVERIFY2(line.contains("-nostdlib") && !line.contains("--sysroot"), line.constData());
        }
    }
};
QTEST_APPLESS_MAIN(TestBuildRunner)
#include "test_build_runner.moc"
