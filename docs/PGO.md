# Profile-guided optimisation of the game code

When an install has a clang profile next to its hot block list, `kartrecomp-setup` compiles the game with it. The profile
lives at `data/mk8d-<version>/game.profdata`.

Measured on Windows (7800X3D, native renderer, game at 60 fps) with the beta 5 code. The profile was recorded on the first race
of cups 1, 4, 7 and 11, and tested on cups 3, 6 and 10, which it never saw. Each test ran a TAS race 3 times with the profile
and 3 times without. The game's CPU threads used 10.7%, 11.6% and 11.3% less CPU time over the same 190 s, and the runs
with and without the profile never overlapped.

- Fast code builds use the profile, with or without direct memory. Other builds ignore it.
- Without a profile, nothing changes.
- The profile is not stored in this repository. It weighs ~35 MB, and it only fits the generated code it was recorded on,
  so it has to be recorded again whenever that code changes (below). It ships in the release package.

## Recording a profile

Use the toolchain and the exporter of the release, so the profile matches what players compile.

1. **Build an instrumented game.** Run `kartrecomp-setup` as usual with these variables:
   ```
   KARTRECOMP_EXTRA_CFLAGS=-fprofile-generate
   KARTRECOMP_EXTRA_LDFLAGS=<toolchain>/lib/clang/<version>/lib/<windows|linux>/libclang_rt.profile-x86_64.a
   ```
   - The flags are part of the build stamp, so the game is compiled again.
   - The runtime library only has to be named when the link step does not pull it in by itself. llvm-mingw needs it.
2. **Play the instrumented game.** Set `LLVM_PROFILE_FILE=<empty folder>/%m.profraw` and play, by hand or with a TAS
   (`tools/make-menu-tas.py`), then quit the game normally.
   - The counts are written at exit, one `.profraw` per game module.
   - Cover what players do: several courses and several cups. The profile only speeds up the code it has seen. The
     numbers above come from the first race of cups 1, 4, 7 and 11 (`make-menu-tas.py --cup=0`, `3`, `6`, `10`).
   - Keep the display clock at 60 Hz (`KARTRECOMP_VSYNC_HZ` unset). Loading screens take real time, so an uncapped game
     plays more frames during them, and the TAS's cup choice lands too early: every race ends up on Mario Kart Stadium.
3. **Merge** with the toolchain's `llvm-profdata`:
   ```
   llvm-profdata merge --sparse -o data/mk8d-<version>/game.profdata <folder>/*.profraw
   ```
   `--sparse` drops the functions that never ran, with the same counts: 34 MB for 4.0.1 on the beta 5 code.
4. **Build again without the variables.** The profile changes the build stamp, so every piece is compiled with it.

Sources are passed to clang relative to the cache folder, so a profile recorded in one install folder matches any other.

## When to record it again

Record again after any change to:
- the exporter or recompiler output;
- the hot block list;
- the toolchain's clang version.

A function whose code no longer matches its profile is compiled as if there were no profile, silently: the warnings are
off, as there would be one line per function. To count the misses, build once with `KARTRECOMP_EXTRA_CFLAGS=-Wbackend-plugin`.
