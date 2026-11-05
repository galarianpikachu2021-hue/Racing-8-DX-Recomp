# SPDX-License-Identifier: GPL-3.0-or-later
# build-windows.ps1 -Out <dir> [-SdkDir <dir>]: builds kr_upscaler_fsr4.dll against the public headers of AMD's FSR SDK 2.4.0 and copies AMD's signed
# Vulkan DLLs next to it (amd_fidelityfx_loader_vk.dll, amd_fidelityfx_upscaler_vk.dll) with AMD's licence text. AMD allows redistributing
# those DLLs in binary form, unmodified, with the licence reproduced; the plugin itself contains no AMD code (it loads the loader at run time).
# Needs: an MSVC developer prompt, the Vulkan SDK headers ($env:VULKAN_SDK), git.
param([Parameter(Mandatory = $true)][string]$Out, [string]$SdkDir = '')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$tag = 'v2.4.0'
if (-not $SdkDir) {
    $SdkDir = Join-Path ([IO.Path]::GetTempPath()) "kr-fsr4-sdk-$([guid]::NewGuid())"
    & git clone --depth 1 --branch $tag https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git $SdkDir
    if ($LASTEXITCODE) { throw 'cloning the FSR SDK failed' }
}
$k = Join-Path $SdkDir 'Kits\FidelityFX'
foreach ($p in "$k\api\include\ffx_api.h", "$k\signedbin\amd_fidelityfx_loader_vk.dll", "$k\signedbin\amd_fidelityfx_upscaler_vk.dll", "$k\docs\license.md") { if (-not (Test-Path $p)) { throw "missing $p" } }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$obj = Join-Path ([IO.Path]::GetTempPath()) "kr-fsr4-obj-$([guid]::NewGuid())"
New-Item -ItemType Directory $obj | Out-Null
& cl /nologo /std:c++17 /O2 /MD /EHsc /c "/I$k\api\include" "/I$k\api\include\vk" "/I$k\upscalers\include" "/I$here\..\fsr3" "/I$env:VULKAN_SDK\Include" "/Fo$obj\plugin.obj" "$here\kr_upscaler_fsr4.cpp"
if ($LASTEXITCODE) { throw 'compiling the FSR 4 plugin failed' }
& link /nologo /DLL "/OUT:$Out\kr_upscaler_fsr4.dll" "$obj\plugin.obj"
if ($LASTEXITCODE) { throw 'linking kr_upscaler_fsr4.dll failed' }
Copy-Item "$k\signedbin\amd_fidelityfx_loader_vk.dll", "$k\signedbin\amd_fidelityfx_upscaler_vk.dll" $Out
Copy-Item "$k\docs\license.md" "$Out\AMD-FSR-SDK-LICENSE.txt"
Remove-Item -Recurse -Force $obj
Get-ChildItem $Out -Include *.exp, *.lib -Recurse | Remove-Item -Force   # import libraries the linker leaves beside the DLLs
Get-ChildItem $Out | ForEach-Object { Write-Host ("{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
