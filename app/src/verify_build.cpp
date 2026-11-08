// SPDX-License-Identifier: GPL-3.0-or-later
#include "verify_build.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#include "build_runner.h"

namespace {
// Blocks run by the guest when the title is in its main loop (a real boot runs billions within a minute); far above the loader's own work.
constexpr qint64 kMainLoopBlocks = 1'000'000'000;

struct Coverage {
    bool present = false;
    qint64 blocks = 0;
    qint64 jit = 0;
    bool killed = false;
};

Coverage ReadCoverage(const QString& path) {
    Coverage c;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return c;
    const QString text = QString::fromUtf8(f.readAll());
    const auto blocks = QRegularExpression(R"(static blocks executed\s*:\s*(\d+))").match(text);
    const auto jit = QRegularExpression(R"(static -> JIT\s*:\s*(\d+))").match(text);
    if (!blocks.hasMatch() || !jit.hasMatch()) return c;
    c.present = true;
    c.blocks = blocks.captured(1).toLongLong();
    c.jit = jit.captured(1).toLongLong();
    c.killed = text.contains("threads killed");
    return c;
}
// Seconds in which the game presented frames at a real rate (PERF sampling lines in the engine's log): a game stuck in an error or wait
// loop can run plenty of code without ever drawing the title.
int PresentingSeconds(const QString& logPath) {
    QFile f(logPath);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    static const QRegularExpression fps(QStringLiteral("PERF game_fps=([0-9.]+)"));
    int seconds = 0;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const auto m = fps.match(QString::fromLatin1(line));
        if (m.hasMatch() && m.captured(1).toDouble() >= 10.0) ++seconds;
    }
    return seconds;
}
constexpr int kFrameSeconds = 3;
} // namespace

VerifyResult VerifyBuild(const QString& engine, const DataLayout& layout, int timeoutSec, const std::atomic<bool>* cancel) {
    const QString coveragePath = layout.cacheRoot + "/verify/recomp_coverage.txt";
    QDir().mkpath(QFileInfo(coveragePath).absolutePath());
    QFile::remove(coveragePath);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("KARTRECOMP_APP_NAME", "kartrecomp");
    env.insert("XDG_DATA_HOME", QFileInfo(layout.dataRoot).absolutePath());
    env.insert("XDG_CACHE_HOME", QFileInfo(layout.cacheRoot).absolutePath());
    env.insert("XDG_CONFIG_HOME", layout.cacheRoot + "/verify/config"); // a throwaway config: the user's own settings must not change the check
    env.insert("KARTRECOMP_VERIFY_BOOT", "1");
    env.insert("SUYU_CMD_PERF_SAMPLE", "1"); // one "PERF game_fps=..." line per second in the log: the proof that frames were presented
    env.insert("SUYU_RECOMP_COVERAGE_PATH", coveragePath);
    env.insert("SDL_VIDEODRIVER", "dummy"); // no window: the Null renderer needs no display
    env.insert("SDL_AUDIODRIVER", "dummy");
    env.remove("KARTRECOMP_NATIVE_GPU");
    env.insert("KARTRECOMP_NO_MODS", "1");
    QProcess p;
    BuildRunner::DieWithParent(p);
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(QFileInfo(engine).absolutePath());
    p.start(engine, {"--module-dir", layout.moduleDir(), "--content-base", layout.base(), "--content-update", layout.update()});
    if (!p.waitForStarted(10000)) return {false, QStringLiteral("Cannot start the game engine (%1).").arg(engine)};
    const auto stop = [&p] {
        p.kill();
        p.waitForFinished(5000);
    };
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutSec * 1000LL) {
        if (cancel && cancel->load()) { stop(); return {false, QStringLiteral("Cancelled.")}; }
        if (p.waitForFinished(250)) {
            const QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
            // An integer division by zero at boot (Windows 0xc0000094, Linux SIGFPE = signal 8) is what firmware newer than 22.5 causes.
            const bool fw_crash = err.contains(QLatin1String("0xc0000094"), Qt::CaseInsensitive) || err.contains(QLatin1String("SIGFPE")) ||
                                  (p.exitStatus() == QProcess::CrashExit && p.exitCode() == 8) || p.exitCode() == 136;
            const QString hint = fw_crash ? QStringLiteral("\n\nThis crash comes from firmware newer than 22.5: pick firmware 22.5.0 or older on the Setup tab and build again.")
                                          : QString();
            return {false, QStringLiteral("The game stopped during its test boot: %1%2").arg(err.isEmpty() ? QStringLiteral("no message (exit %1)").arg(p.exitCode()) : err, hint)};
        }
        const Coverage c = ReadCoverage(coveragePath);
        if (!c.present) continue;
        if (c.jit > 0 || c.killed) {
            stop();
            return {false, c.killed ? QStringLiteral("The game's test boot lost a thread: some code was not recompiled.")
                                    : QStringLiteral("The build is not fully static (%1 transitions to a JIT, which this project never allows).").arg(c.jit)};
        }
        if (c.blocks >= kMainLoopBlocks && PresentingSeconds(layout.dataRoot + "/log/suyu_log.txt") >= kFrameSeconds) {
            stop();
            return {true, {}};
        }
    }
    const Coverage last = ReadCoverage(coveragePath);
    stop();
    if (last.present && last.blocks >= kMainLoopBlocks)
        return {false, QStringLiteral("The game ran but never presented frames in %1 s of the test boot: it is probably stuck (check the firmware and keys).").arg(timeoutSec)};
    return {false, QStringLiteral("The game did not reach its main loop within %1 s of the test boot.").arg(timeoutSec)};
}
