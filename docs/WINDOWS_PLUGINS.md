# Upscaler plugins on Windows (researched 2026-11-05)

The native renderer loads upscalers and frame generators from `engine\plugins\` (`kr_upscaler_<id>.dll`, `kr_framegen_<id>.dll`; the plugin ABI is
in `docs/UPSCALER.md`). What exists and what ships:

| Plugin | Source | Ships in the zip | Notes |
|---|---|---|---|
| **FSR 3.1** upscaler + **FSR 3 frame generation** (`kr_upscaler_fsr3.dll`, `kr_framegen_fsr3.dll`) | AMD FidelityFX SDK **v1.1.4** (MIT), built on CI with MSVC from the SDK's own sources and shader blobs | yes | Works on any Vulkan GPU. The framegen file is a tiny forwarder to the upscaler DLL (one loaded module, like the Linux symlink). |
| **FSR 4.1.2** (`kr_upscaler_fsr4.dll`) | AMD FSR SDK **2.4.0** (2026-10-28): official **Vulkan** support, "feature parity with DX12" | yes, with AMD's two signed DLLs | The plugin holds no AMD code; it loads `amd_fidelityfx_loader_vk.dll` beside it. AMD's licence allows redistributing the DLLs in binary form, unmodified, with the licence text (shipped as `AMD-FSR-SDK-LICENSE.txt`). |
| **DLSS** super resolution + **DLSS frame generation** (`kr_upscaler_dlss.dll`, `kr_framegen_dlss.dll`) | NVIDIA's public NGX SDK (github.com/NVIDIA/DLSS) | **never** | It links NVIDIA's static NGX library and needs NVIDIA's `nvngx_dlss*.dll`. Built on your own machine (`plugins\dlss\build-windows.ps1`) or from the manual-only CI job; the release test fails if anything DLSS/NGX is in the zip. |

## FSR 4 models (what "Model" in the in-game menu lists)

The signed `amd_fidelityfx_upscaler_vk.dll` carries these providers (checked in the binary): FSR 4 (FP8), FSR 4 INT8, FSR 4 INT8 for RDNA 2,
**FSR 4 Lite INT8 (the lightweight model)**, FSR 3.1 and FSR 2. With "Auto" the SDK picks per GPU, per AMD's documentation:

| Hardware | Model |
|---|---|
| Radeon RX 9000 (RDNA 4) | full FP8 model |
| Radeon RX 7000 / RX 6000, Ryzen AI Max 300 / 400 | INT8 model |
| Ryzen AI 9 300 / 400, Ryzen Z2 Extreme / Z2 A (handhelds, laptops) | lightweight INT8 model (under half the size) |
| anything else (NVIDIA, Intel, older AMD) | the SDK falls back to FSR 3.1.6 |

The Model row appears under Upscaler when the chosen upscaler offers more than one model: FSR 4 lists the providers the runtime reports for the
GPU (`ffxQuery` GET_VERSIONS), DLSS lists its render presets (Auto, K, J, M, L, E). A model that cannot run on the GPU falls back to Auto and logs it.
The choice is saved (`model=` in `kartrecomp_overlay.ini`). The Vulkan path of FSR 4 needs AMD Adrenalin 26.10.1 or newer; the driver may carry a
newer FSR version than the DLL ("Auto" prefers it).

Not done: FSR 4 **frame generation** (SDK 2.4 has it on Vulkan, but its swapchain-based flow is a separate piece of work), HDR-linear input for FSR 4
(the host gives a tonemapped, display-encoded image; the plugin sets the non-linear colour flags).

## Build

CI (`.github/workflows/windows-engine.yml`): `ffx-prepared` (Linux: the SDK with our edits plus the shader blobs), then `plugins` (Windows/MSVC:
`plugins\fsr3\build-windows.ps1`, `plugins\fsr4\build-windows.ps1`, `tests\windows\test_plugins.ps1`), then `bundle`. DLSS: the `dlss-local` job
(runs only when a pushed commit message contains `[dlss]`; 1-day artifact) or `plugins\dlss\build-windows.ps1` on a machine with Visual Studio Build Tools and the Vulkan SDK.

## Test by hand

`tests\windows\test_plugins.ps1` needs no GPU. On a GPU: run the game, press F10, set Upscaler to FSR 3 / FSR 4 / DLSS and 3D render scale to 67% or
50%; the line `Running: ...` and the Model row's note show what is active. Logs: `userdata\...\log`. Environment switches: `KARTRECOMP_FSR4_DEBUG=1`
(SDK debug checking), `KARTRECOMP_FSR4_NOREACTIVE=1`.
