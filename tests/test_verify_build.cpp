// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "verify_build.h"

namespace {
// A stand-in engine: a shell script. It reads what it should do from FAKE_MODE and writes the coverage report where the real engine
// would (SUYU_RECOMP_COVERAGE_PATH).
QString MakeEngine(const QTemporaryDir& d) {
    const QString p = d.filePath("engine.sh");
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write("#!/bin/sh\n"
            "cov=\"$SUYU_RECOMP_COVERAGE_PATH\"\n"
            "report() { printf '=== RECOMP EXECUTION COVERAGE ===\\n  static blocks executed : %s\\n  static -> JIT          : %s (0 lookup miss, 0 unimplemented opcode)\\n%s=== END ===\\n' \"$1\" \"$2\" \"$3\" > \"$cov\"; }\n"
            "case \"$FAKE_MODE\" in\n"
            "  good) sleep 0.3; report 2000000000 0 ''; sleep 30;;\n"
            "  jit) sleep 0.3; report 2000000000 5 ''; sleep 30;;\n"
            "  killed) sleep 0.3; report 2000000000 0 '  threads killed with no JIT fallback: 1\\n'; sleep 30;;\n"
            "  early) echo 'cannot open the update' >&2; exit 3;;\n"
            "  slow) sleep 0.3; report 5 0 ''; sleep 30;;\n"
            "  silent) sleep 30;;\n"
            "esac\n");
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    return p;
}
VerifyResult Run(const QString& mode, int timeoutSec = 3) {
    QTemporaryDir d;
    const QString engine = MakeEngine(d);
    qputenv("FAKE_MODE", mode.toUtf8());
    const auto layout = DataLayout::FromRoots(d.filePath("data/kartrecomp"), d.filePath("cache/kartrecomp"));
    return VerifyBuild(engine, layout, timeoutSec, nullptr);
}
} // namespace

class TestVerifyBuild : public QObject {
    Q_OBJECT
private slots:
    void aFullyStaticBootThatReachedTheGameLoopPasses() {
        const auto r = Run("good");
        QVERIFY2(r.ok, qPrintable(r.message));
    }
    void anyTransitionToTheJitFails() {
        const auto r = Run("jit");
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("not fully static"), qPrintable(r.message));
    }
    void aThreadKilledWithNoFallbackFails() {
        const auto r = Run("killed");
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("thread"), qPrintable(r.message));
    }
    void anEngineThatExitsEarlyReportsItsOwnMessage() {
        const auto r = Run("early");
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("cannot open the update"), qPrintable(r.message));
    }
    void aRealBuildBootsHeadlessAndIsAccepted() {
        // KARTRECOMP_REAL_ENGINE=<mk8d-engine> KARTRECOMP_REAL_DATA=<data root with content/, keys/, nand/ and module/>: skipped without them
        const QString engine = qEnvironmentVariable("KARTRECOMP_REAL_ENGINE"), data = qEnvironmentVariable("KARTRECOMP_REAL_DATA");
        if (engine.isEmpty() || data.isEmpty()) QSKIP("no real engine/data given");
        QTemporaryDir cache;
        const auto layout = DataLayout::FromRoots(data, cache.filePath("kartrecomp"));
        const auto r = VerifyBuild(engine, layout, 150, nullptr);
        QVERIFY2(r.ok, qPrintable(r.message));
    }
    void aBootThatNeverGetsGoingTimesOutWithAClearMessage() {
        const auto r = Run("slow", 2);
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains("did not reach"), qPrintable(r.message));
        const auto s = Run("silent", 2);
        QVERIFY(!s.ok);
    }
};
QTEST_APPLESS_MAIN(TestVerifyBuild)
#include "test_verify_build.moc"
