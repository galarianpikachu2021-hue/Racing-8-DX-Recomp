// SPDX-License-Identifier: GPL-3.0-or-later
#include "verify_build.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

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
    env.insert("SUYU_RECOMP_COVERAGE_PATH", coveragePath);
    env.insert("SDL_VIDEODRIVER", "dummy"); // no window: the Null renderer needs no display
    env.insert("SDL_AUDIODRIVER", "dummy");
    env.remove("KARTRECOMP_NATIVE_GPU");
    env.insert("KARTRECOMP_NO_MODS", "1");
    QProcess p;
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
            return {false, QStringLiteral("The game stopped during its test boot: %1").arg(err.isEmpty() ? QStringLiteral("no message (exit %1)").arg(p.exitCode()) : err)};
        }
        const Coverage c = ReadCoverage(coveragePath);
        if (!c.present) continue;
        if (c.jit > 0 || c.killed) {
            stop();
            return {false, c.killed ? QStringLiteral("The game's test boot lost a thread: some code was not recompiled.")
                                    : QStringLiteral("The build is not fully static (%1 transitions to a JIT, which this project never allows).").arg(c.jit)};
        }
        if (c.blocks >= kMainLoopBlocks) {
            stop();
            return {true, {}};
        }
    }
    stop();
    return {false, QStringLiteral("The game did not reach its main loop within %1 s of the test boot.").arg(timeoutSec)};
}
