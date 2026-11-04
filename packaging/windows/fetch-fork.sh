#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# fetch-fork.sh <dir>: the pinned upstream suyu fork (docs/PIPELINE.md) with our patches/suyu/*.patch applied, ready to configure.
# Used by the Windows CI; works anywhere with git and bash. Fails when a patch does not apply.
set -euo pipefail
dir="$1"
here="$(cd "$(dirname "$0")/../.." && pwd)"
repo=https://github.com/dougchansan/suyu-v0.0.4.git
sha=5949cab3ba93233ddd1c319bfc6f010f6cfa910a
rm -rf "$dir"
git clone --filter=blob:none --no-checkout "$repo" "$dir"
git -C "$dir" checkout --quiet "$sha"
git -C "$dir" submodule update --init --recursive --depth 1
for p in "$here"/patches/suyu/*.patch; do
  echo "applying $(basename "$p")"
  git -C "$dir" apply --whitespace=nowarn "$p"
done
