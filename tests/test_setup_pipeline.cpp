// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "build_runner.h"
#include "test_support.h"

#include "setup_pipeline.h"

namespace {
void Write(const QString& path, const QByteArray& bytes, bool exec = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
    if (exec) f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
}

// Fake exporter / compiler / engine as shell scripts, so the pipeline runs end to end without game files.
struct Fixture {
    QTemporaryDir dir;
    SetupInputs in;
    DataLayout layout;
    BuilderPaths paths;
    Fixture(const QString& updateName = "update.nsp") {
        Write(dir.filePath("in/base.nsp"), "B");
        Write(dir.filePath("in/" + updateName), "U");
        Write(dir.filePath("in/prod.keys"), "header_key = 00\naes_kek_generation_source = 00\nkey_area_key_application_source = 00\n"
                                              "aes_key_generation_source = 00\ntitlekek_source = 00\nmaster_key_00 = 00\n");
        Write(dir.filePath("in/fw/a.nca"), "F");
        in = {dir.filePath("in/base.nsp"), dir.filePath("in/" + updateName), dir.filePath("in/prod.keys"), dir.filePath("in/fw"), {}};
        layout = DataLayout::FromRoots(dir.filePath("data/kartrecomp"), dir.filePath("cache/kartrecomp"));
        paths.exporter = dir.filePath("exporter.sh");
        Write(paths.exporter,
              "#!/bin/sh\n"
              "if [ \"$1\" = --info ]; then case \"$2\" in\n"
              "  *base*) echo '{\"titleId\":\"0100152000022000\",\"version\":0,\"aarch64\":true,\"hasProgram\":true,\"error\":\"\"}';;\n"
              "  *old*) echo '{\"titleId\":\"0100152000022800\",\"version\":1245184,\"aarch64\":false,\"hasProgram\":true,\"error\":\"\"}';;\n"
              "  *) echo '{\"titleId\":\"0100152000022800\",\"version\":1310720,\"aarch64\":true,\"hasProgram\":true,\"error\":\"\"}';; esac; exit 0; fi\n"
              "[ -n \"$FAKE_EXPORT_FAILS\" ] && { echo 'cannot decrypt' >&2; exit 3; }\n"
              "while [ $# -gt 0 ]; do [ \"$1\" = --out ] && out=\"$2\"; shift; done\n"
              "mkdir -p \"$out/main/src\"; : > \"$out/main/recomp_export.c\"; : > \"$out/main/recomp_runtime.c\"; : > \"$out/main/src/recompiled_main.c\"\n"
              "printf 'static uint32_t recomp_gg_seen[4];\\nvoid blk_main_0000000000000000(GuestContext* c){\\n}\\nconst struct _recomp_ent _seg_main_0[] = {\\n};\\n' > \"$out/main/src/recompiled_main_0.c\"\n"
              "echo 'PROGRESS 100 done'\n", true);
        paths.toolchain = dir.filePath("tc");
        Write(paths.toolchain + "/bin/clang", "#!/bin/sh\nout=; while [ $# -gt 0 ]; do [ \"$1\" = -o ] && out=\"$2\"; shift; done; echo o > \"$out\"\n", true);
        paths.engine = dir.filePath("engine.sh");
        paths.engineVersion = "v1";
        Write(paths.engine,
              "#!/bin/sh\nlog=\"$XDG_DATA_HOME/kartrecomp/log/suyu_log.txt\"; mkdir -p \"$(dirname \"$log\")\"\n"
              "for i in 1 2 3 4; do echo \"PERF game_fps=60.0\" >> \"$log\"; done\n"
              "printf '=== X ===\\n  static blocks executed : 2000000000\\n  static -> JIT          : 0 (0 lookup miss, 0 unimplemented opcode)\\n' > \"$SUYU_RECOMP_COVERAGE_PATH\"\nsleep 30\n", true);
        paths.hotList = dir.filePath("hot.txt");
        Write(paths.hotList, "blk_main_0000000000000000\n");
    }
};
} // namespace

class TestSetupPipeline : public QObject {
    Q_OBJECT
private slots:
    void aGoodRunExportsBuildsVerifiesAndStamps() {
        NEEDS_SH();
        Fixture f;
        QList<int> marks;
        const auto r = RunSetupPipeline(f.in, f.layout, f.paths, [&](int permille, const QString&) { marks << permille; }, nullptr, {0, 0});
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(IsBuilt(f.layout, "v1"));
        QVERIFY(QFile::exists(f.layout.moduleDir() + "/" + BuildRunner::ModuleFileName("image"))); // the main module is called "image"
        QVERIFY(QFile::exists(f.layout.base()) && QFile::exists(f.layout.keysFile()));
        QVERIFY(std::is_sorted(marks.begin(), marks.end()));
        QCOMPARE(marks.last(), 1000);
    }
    void aNewEngineVersionRebuildsEverythingAndAStaleExportIsCleared() {
        NEEDS_SH();
        Fixture f;
        const auto touch = [&](const QString& path) { Write(path, "x"); };
        QVERIFY(RunSetupPipeline(f.in, f.layout, f.paths, nullptr, nullptr, {0, 0}).ok);
        touch(f.layout.workDir() + "/main/obj/sentinel.o");
        touch(f.layout.exportDir() + "/stale-from-an-older-export.txt");
        QVERIFY(RunSetupPipeline(f.in, f.layout, f.paths, nullptr, nullptr, {0, 0}).ok);
        QVERIFY(QFile::exists(f.layout.workDir() + "/main/obj/sentinel.o"));      // same engine: resumed
        QVERIFY(!QFile::exists(f.layout.exportDir() + "/stale-from-an-older-export.txt")); // the export is regenerated every time
        f.paths.engineVersion = "v2";
        QVERIFY(RunSetupPipeline(f.in, f.layout, f.paths, nullptr, nullptr, {0, 0}).ok);
        QVERIFY(!QFile::exists(f.layout.workDir() + "/main/obj/sentinel.o"));     // new engine: everything compiled again
        QVERIFY(IsBuilt(f.layout, "v2"));
    }
    void an32BitUpdateStopsBeforeAnythingIsInstalled() {
        NEEDS_SH();
        Fixture f("old-update.nsp");
        const auto r = RunSetupPipeline(f.in, f.layout, f.paths, nullptr, nullptr, {0, 0});
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("32-bit"), qPrintable(r.message));
        QVERIFY(!QFile::exists(f.layout.base())); // nothing was installed
    }
    void aFailingExportReportsTheExportersOwnMessage() {
        NEEDS_SH();
        Fixture f;
        qputenv("FAKE_EXPORT_FAILS", "1");
        const auto r = RunSetupPipeline(f.in, f.layout, f.paths, nullptr, nullptr, {0, 0});
        qunsetenv("FAKE_EXPORT_FAILS");
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("cannot decrypt"), qPrintable(r.message));
        QVERIFY(!IsBuilt(f.layout, "v1"));
    }
    void aCancelledRunLeavesNothingMarkedBuilt() {
        NEEDS_SH();
        Fixture f;
        std::atomic<bool> cancel{true};
        const auto r = RunSetupPipeline(f.in, f.layout, f.paths, nullptr, &cancel, {0, 0});
        QVERIFY(!r.ok);
        QVERIFY(!IsBuilt(f.layout, "v1"));
    }
    void aFailedVerificationNeverStampsTheModules() {
        NEEDS_SH();
        Fixture f;
        Write(f.paths.engine, "#!/bin/sh\necho 'boot failed' >&2\nexit 3\n", true);
        const auto r = RunSetupPipeline(f.in, f.layout, f.paths, nullptr, nullptr, {0, 0});
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("boot failed"), qPrintable(r.message));
        QVERIFY(!IsBuilt(f.layout, "v1"));
    }
};
QTEST_APPLESS_MAIN(TestSetupPipeline)
#include "test_setup_pipeline.moc"
