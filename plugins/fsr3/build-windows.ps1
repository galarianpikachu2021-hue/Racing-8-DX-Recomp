# SPDX-License-Identifier: GPL-3.0-or-later
# build-windows.ps1 -Prepared <dir> -Out <dir>: builds kr_upscaler_fsr3.dll (FSR 3.1 upscaler + FSR 3 frame generation in one library) and
# kr_framegen_fsr3.dll (a forwarder to it) from AMD's FidelityFX SDK v1.1.4 (MIT), with MSVC. <Prepared> comes from plugins/fsr3/prepare-sdk.sh
# (the SDK with our edits and its shader blobs, generated on Linux CI: the blobs are plain SPIR-V). Nothing from the SDK is committed.
# Needs: an MSVC developer prompt (cl, link) and the Vulkan SDK ($env:VULKAN_SDK).
param([Parameter(Mandatory = $true)][string]$Prepared, [Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$Prepared = (Resolve-Path $Prepared).Path
$sdk = Join-Path $Prepared 'sdk\sdk'
$shaders = Join-Path $Prepared 'shaders'
foreach ($p in $sdk, $shaders, $env:VULKAN_SDK) { if (-not $p -or -not (Test-Path $p)) { throw "missing $p" } }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$obj = Join-Path ([IO.Path]::GetTempPath()) "kr-fsr3-obj-$([guid]::NewGuid())"
New-Item -ItemType Directory $obj | Out-Null
$inc = @("$sdk\include", "$sdk\src\shared", "$sdk\src\backends\shared", "$sdk\src\backends\shared\blob_accessors", "$sdk\src\components", "$sdk\src\backends\vk", $shaders, "$env:VULKAN_SDK\Include") | ForEach-Object { "/I$_" }
$defs = '/DFFX_FSR3UPSCALER=1', '/DFFX_FI=1', '/DFFX_OF=1', '/DFFX_API_VK=1', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS'
$srcs = @("$sdk\src\components\fsr3upscaler\ffx_fsr3upscaler.cpp", "$sdk\src\backends\vk\ffx_vk.cpp", "$sdk\src\shared\ffx_assert.cpp",
    "$sdk\src\components\frameinterpolation\ffx_frameinterpolation.cpp", "$sdk\src\components\opticalflow\ffx_opticalflow.cpp",
    "$sdk\src\backends\shared\blob_accessors\ffx_frameinterpolation_shaderblobs.cpp", "$sdk\src\backends\shared\blob_accessors\ffx_opticalflow_shaderblobs.cpp",
    "$sdk\src\shared\ffx_message.cpp", "$sdk\src\shared\ffx_breadcrumbs_list.cpp", "$sdk\src\shared\ffx_object_management.cpp",
    "$sdk\src\backends\shared\ffx_shader_blobs.cpp", "$sdk\src\backends\shared\blob_accessors\ffx_fsr3upscaler_shaderblobs.cpp")
& cl /nologo /std:c++17 /O2 /MD /EHsc /w /MP /c $defs $inc "/Fo$obj/" @srcs
if ($LASTEXITCODE) { throw 'compiling the FidelityFX SDK sources failed' }
& cl /nologo /std:c++17 /O2 /MD /EHsc /c "/I$sdk\include" "/I$env:VULKAN_SDK\Include" "/I$here" "/Fo$obj\plugin.obj" "$here\kr_upscaler_fsr3.cpp"
if ($LASTEXITCODE) { throw 'compiling the plugin failed' }
& link /nologo /DLL "/OUT:$Out\kr_upscaler_fsr3.dll" "$obj\*.obj" "$env:VULKAN_SDK\Lib\vulkan-1.lib"
if ($LASTEXITCODE) { throw 'linking kr_upscaler_fsr3.dll failed' }
& cl /nologo /O2 /LD "$here\..\windows\forwarder.c" "/Fe:$Out\kr_framegen_fsr3.dll" "/Fo$obj\forwarder.obj"
if ($LASTEXITCODE) { throw 'building the frame-generation forwarder failed' }
Remove-Item -Recurse -Force $obj
Get-ChildItem $Out -Filter 'kr_*fsr3.dll' | ForEach-Object { Write-Host ("{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
