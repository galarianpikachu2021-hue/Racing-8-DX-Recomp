#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# glibc-floor.sh <dir> [max version]: prints the newest glibc symbol version any ELF file under <dir> needs, and which file. With a max
# version, exits 1 when something needs more (the AppImage would not start on distros older than that).
set -euo pipefail
dir="$1"; max="${2:-}"
newest=0; who=""
while IFS= read -r f; do
  v=$(objdump -T "$f" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sed 's/GLIBC_//' | sort -V | tail -1 || true)
  [ -n "$v" ] || continue
  if [ "$(printf '%s\n%s\n' "$newest" "$v" | sort -V | tail -1)" = "$v" ] && [ "$v" != "$newest" ]; then newest="$v"; who="$f"; fi
done < <(find "$dir" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) -exec sh -c 'for f; do head -c4 "$f" 2>/dev/null | grep -q "ELF" && echo "$f"; done' _ {} +)
echo "glibc $newest (${who#$dir/})"
[ -z "$max" ] || [ "$(printf '%s\n%s\n' "$max" "$newest" | sort -V | tail -1)" = "$max" ] || { echo "needs glibc $newest, more than $max" >&2; exit 1; }
