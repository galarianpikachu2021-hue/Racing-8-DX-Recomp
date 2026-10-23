# FSR 4.1.1 quality test (contained, offline)

FSR 4 only exists as AMD's signed DX12 DLLs, so it can't load into the native Vulkan game. This test runs it on frames the game
dumped, under Proton, and compares it with our FSR 3.1 plugin on exactly the same inputs. Nothing here changes the game.

1. **Dump frames** (Linux): build `plugins/dump/kr_upscaler_dump.cpp` into `plugins/libkr_upscaler_dump.so` (`g++ -std=c++17 -O2 -fPIC
   -shared -I plugins/fsr3 kr_upscaler_dump.cpp -ldl`), put it and `libkr_upscaler_fsr3.so` in `local/game/plugins/`, then run the game
   with the overlay setting `upscaler=dump` (3D render scale 50%, jitter on) and
   `KARTRECOMP_DUMP_DIR=<dir> KARTRECOMP_DUMP_START=500 KARTRECOMP_DUMP_COUNT=120`. You get `frame_NNNN.bin` (colour, depth, motion
   vectors, jitter ...) and `out_NNNN.raw` (what our FSR 3.1 produced).
2. **Run FSR 4** (Windows exe under GE-Proton): fetch AMD's FidelityFX SDK v2.3.0 (`git clone --branch v2.3.0 ...FidelityFX-SDK`), take
   `Kits/FidelityFX/signedbin/amd_fidelityfx_{loader,upscaler}_dx12.dll` (local only, never commit them), cross-compile `fsr4_runner.cpp`
   (no mingw here: `pip install ziglang`, then `python -m ziglang c++ -target x86_64-windows-gnu -O2 -std=c++17 -D_WINDOWS
   -I<sdk>/Kits/FidelityFX/api/include -I<sdk>/Kits/FidelityFX/upscalers/include -I<sdk>/Kits/FidelityFX/api/include/dx12
   fsr4_runner.cpp -o fsr4_runner.exe -ld3d12 -ldxgi -ldxguid`), and run
   `PROTON_FSR4_UPGRADE=1 proton run fsr4_runner.exe Z:/<dump dir> Z:/<out dir> 4.1.1` (GE-Proton's FSR 4 upgrade switch, set so the Proton side is forced to FSR 4) (a version argument picks the provider: `4.1.1`, `3.1.5`, `2.3.4`;
   a fourth argument `linear` passes the colour as linear). Output: `fsr4_NNNN.raw` (RGBA16F) and `log.txt`.
3. **Compare**: `compare.py` (edge energy and PSNR). `replay_host.cpp` (Linux, Vulkan) replays a dump through one of our plugins in seconds,
   for iterating on the plugin without the game.

Result on a 120-frame race dump (render 1280x720 -> 2560x1440): edge energy bicubic 0.0226, our FSR 3.1 0.0183, AMD FSR 3.1.5 0.0182,
FSR 4.1.1 0.0200; our plugin vs AMD's 3.1.5: 58.9 dB (nearly identical), AMD 3.1.5 vs FSR 4.1.1: 29.6 dB.
