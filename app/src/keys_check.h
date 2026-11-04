// SPDX-License-Identifier: GPL-3.0-or-later
// Checks the user's prod.keys has what opening the game's files needs, before anything slow starts.
#pragma once

#include <QString>

enum class KeyProblem { None, NoFile, MissingHeaderKey, MissingMasterKey, MissingDerivationKey };

KeyProblem CheckKeys(const QString& prodKeysPath);
QString KeyProblemText(KeyProblem problem);
