# Legal position

Not legal advice. This file states the rules this project holds itself to.

## What this repository contains

- The source of the KartRecomp launcher (Qt6, C++), our build tooling and our documentation.
- Patches against a GPL-licensed fork of the suyu emulator (`patches/suyu/`), and pointers to
  third-party repositories as git submodules. We do not copy third-party code that has no license.

## What it never contains

- **Game data:** no ROM, dump, NSP, XCI, NCA, NSO, extracted ExeFS or RomFS, assets, or parts of any of these.
- **Keys:** no `prod.keys` or `title.keys`, no key material of any kind, and no instructions for getting them
  or links to them.
- **Firmware:** none, in any form.
- **Recompiled game code:** the recompiler's output (generated C, object files, libraries, and the playable
  binary) is a mechanical translation of the game's copyrighted code. It is generated on the user's own
  machine from their own dump. It is gitignored and is never published in any form.

## How a user gets a playable build

Each user supplies their own legally obtained dump of the game, plus their own keys and firmware dumped from
their own console. The app then runs the export and compile on that user's machine. Nothing game-derived
leaves that machine through this project.

## Trademarks

"Nintendo", "Nintendo Switch" and "Mario Kart" are trademarks of Nintendo. This project isn't affiliated
with or endorsed by Nintendo, and uses those names only to describe compatibility. No Nintendo artwork,
logos or game imagery belong in this repository, including in screenshots.

## Licensing

Our own code is licensed under GPL-3.0-or-later (see `LICENSE`), which is compatible with the suyu code it
builds on.

## Third-party upscaler libraries

- **AMD FSR (FidelityFX SDK).** The FSR 3 plugin is built from AMD's MIT-licensed SDK sources. The FSR 4 plugin loads AMD's signed
  `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` (SDK 2.4.0), which AMD permits to be redistributed in binary form,
  unmodified, with its licence text; the Windows zip includes them with `AMD-FSR-SDK-LICENSE.txt`. We do not modify, decompile or
  disassemble them.
- **NVIDIA DLSS / NGX.** Never redistributed by this project: no NVIDIA library, and no plugin that links one, is in a release, the zip or the
  repository. The DLSS plugin is built by the user on their own machine (`plugins/dlss/`).
