// SPDX-License-Identifier: GPL-3.0-or-later
// After a build: boot the game headless (Null renderer, no window, no GPU) and read the engine's own coverage report. The game is
// accepted only when it reaches its main loop with every guest instruction run as recompiled code (no transition to a JIT, no thread
// killed for lack of one). Only then does the builder stamp the modules and enable Play.
#pragma once

#include <QString>
#include <atomic>

#include "setup_paths.h"

struct VerifyResult {
    bool ok = false;
    QString message; // what failed (or "" when ok)
};

/// `engine` is started with the installed files of `layout` and killed as soon as the verdict is known. `timeoutSec` bounds the boot.
VerifyResult VerifyBuild(const QString& engine, const DataLayout& layout, int timeoutSec, const std::atomic<bool>* cancel);
