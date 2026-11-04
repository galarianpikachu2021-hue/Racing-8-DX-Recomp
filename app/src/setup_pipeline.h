// SPDX-License-Identifier: GPL-3.0-or-later
// The whole "files -> playable game" sequence as one blocking call: check the inputs, install them into the data layout, export the C,
// compile the modules, verify a headless boot, stamp the modules. The Setup page runs it on a worker thread; kartrecomp-setup runs it from
// a shell (acceptance runs, support).
#pragma once

#include <QString>
#include <atomic>
#include <functional>

#include "setup_paths.h"
#include "setup_validate.h"

struct PipelineResult {
    bool ok = false;
    QString message; // why it failed, or "" when ok
};

/// Free space the pipeline insists on (bytes). Tests set both to 0.
struct PipelineLimits {
    qint64 cacheBytes = 7LL << 30; // work files of the build
    qint64 dataBytes = 2LL << 30;  // the finished game and the installed files
};

/// `progress(permille, text)`: 0..1000 over validate (0-10), export (10-100), compile (100-950), verify (950-990), stamp (1000).
PipelineResult RunSetupPipeline(const SetupInputs& inputs, const DataLayout& layout, const BuilderPaths& paths,
                                const std::function<void(int, const QString&)>& progress, const std::atomic<bool>* cancel,
                                const PipelineLimits& limits = {});
