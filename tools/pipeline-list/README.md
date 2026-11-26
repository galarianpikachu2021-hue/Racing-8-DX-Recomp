# Pipeline list tools

Offline tools that turn a pipeline recording into the list shipped for install-time pipeline precompilation. The whole
procedure (recording, RomFS dump, where the list goes) is in [docs/PRECOMPILE.md](../../docs/PRECOMPILE.md).

- `shader_index`: indexes the NVN shader code blobs of a RomFS dump, so a stage's code can be traced back to a file and an
  offset in the game data. It also extracts a (Yaz0-decompressed) file for `build-list.py`.
- `build-list.py`: replaces each recorded stage's code by its RomFS path and offset (or `exefs:<nso>`), so the list holds no
  game bytes. Its docstring describes the list format.

## Build

`build-list.py` and `selftest.py` look for `shader_index` (or `shader_index.exe`) in this folder, or take its path from the
`SHADER_INDEX` environment variable. Standard C++17, no dependency:

    c++ -O2 -std=c++20 -pthread shader_index.cpp -o shader_index      # Linux
    cl /nologo /std:c++17 /O2 /EHsc shader_index.cpp                 # Windows, from a "x64 Native Tools" prompt

## Commands

    shader_index index   <romfs root> <index file>
    shader_index extract <romfs root> <relative path> <offset hex> <out file> [<length hex>, default 0x10000]
    shader_index lookup  <index file> <code file>
    python -I build-list.py <recording file> <romfs root> <k80 index> <out list> <game version> [--exefs <nso dir>]
    python -I build-list.py --verify <out list> <romfs root> [--exefs <nso dir>]
    python -I selftest.py [<work dir>]

- **index** walks every file under the root (sorted, one thread per logical CPU). A file that starts with `Yaz0` is
  decompressed first. The buffer is searched byte by byte for the 0x30-byte prefix (`78 56 34 12` + 0x2C zero bytes); a
  prefix at a 0x10-aligned offset whose SPH word (prefix+0x30) has `word & 0x1f` equal to 1 or 2 gives one line:

      <key> <relative path, forward slashes> <offset, 0x hex> <shader type: (word >> 10) & 0xf>

  `Yaz0` streams nested at a 4-aligned offset (Yaz0-compressed SARCs inside a `.sarc`, for example) are decompressed and
  scanned too, up to 4 levels, with the path `<file>@0x<stream offset>[@0x...]`. Offsets point into the decompressed bytes.
  Totals go to stdout; unaligned prefixes, prefixes without a valid SPH and Yaz0 errors are counted, not indexed.
- **key** is the lowercase hex SHA-1 of the bytes from prefix+0x30 to prefix+0x230, clamped to the end of the buffer
  (`hashlib.sha1(buf[off+0x30:off+0x230]).hexdigest()`). `SHADER_INDEX_KEYLEN=0x80` sets a 0x80-byte window instead:
  `build-list.py` needs that short-key index, since a stage's code can be shorter than 0x200 bytes.
- **extract** loads a file the same way (then each `@0x...` stream in turn) and writes `length` bytes from `offset`,
  clamped to the end; `ffffffff` dumps the whole buffer.
- **lookup** prints the index lines matching the key of a capture that starts at the prefix, or `not found (key ...)`
  (exit code 2).
- **selftest.py** builds a small synthetic RomFS (no game data) and checks `index` against an independent Python search and
  `hashlib.sha1` for both key lengths, then `extract`, `lookup` and `build-list.py`.
