// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "build_runner.h"

class TestBuildRunner : public QObject {
    Q_OBJECT
private slots:
    void jobsFollowFreeRamAtTwoGigabytesEach() {
        constexpr qint64 GiB = 1024LL * 1024 * 1024;
        QCOMPARE(BuildRunner::JobsForRam(16 * GiB, 8), 8);
        QCOMPARE(BuildRunner::JobsForRam(8 * GiB, 8), 4);
        QCOMPARE(BuildRunner::JobsForRam(8 * GiB, 2), 2);
        QCOMPARE(BuildRunner::JobsForRam(3 * GiB, 8), 1);
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
        QVERIFY(QFile::exists(dir.filePath("out/recompiled_rtld.so")));
        // compile args go to every compile, link args only to the link
        for (const QByteArray& line : calls.split('\n')) {
            if (line.contains(" -c ")) QVERIFY2(line.contains("--sysroot=/toolchain/sysroot") && !line.contains("-nostdlib"), line.constData());
            if (line.contains("-shared")) QVERIFY2(line.contains("-nostdlib") && !line.contains("--sysroot"), line.constData());
        }
    }
};
QTEST_APPLESS_MAIN(TestBuildRunner)
#include "test_build_runner.moc"
