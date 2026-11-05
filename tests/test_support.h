// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Several tests stand in for the exporter, compiler and engine with small /bin/sh scripts. Windows has no /bin/sh: those tests are
// skipped there, and the real tools are exercised by the Windows acceptance run (tests/windows/*.ps1 and the on-PC check).
#ifdef Q_OS_WIN
#define NEEDS_SH() QSKIP("the fake tools of this test are /bin/sh scripts")
#else
#define NEEDS_SH()
#endif
