# SPDX-License-Identifier: GPL-3.0-or-later
# build-windows.ps1 -Out <dir>: builds kr_upscaler_dlss.dll (DLSS super resolution + DLSS frame generation through NGX) and the kr_framegen_dlss.dll
# forwarder from NVIDIA's public DLSS SDK (github.com/NVIDIA/DLSS), and copies the NGX runtime snippets (nvngx_dlss.dll, nvngx_dlssg.dll) beside them.
# THIS IS FOR YOUR OWN MACHINE: the result links NVIDIA's static NGX library and ships NVIDIA's runtime, so it is never put in a release or
# committed (see LEGAL.md). Needs: an MSVC developer prompt, the Vulkan SDK headers ($env:VULKAN_SDK), git.
param([Parameter(Mandatory = $true)][string]$Out, [string]$SdkDir = '')
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $SdkDir) {
    $SdkDir = Join-Path ([IO.Path]::GetTempPath()) "kr-dlss-sdk-$([guid]::NewGuid())"
    & git clone --depth 1 https://github.com/NVIDIA/DLSS.git $SdkDir
    if ($LASTEXITCODE) { throw 'cloning the DLSS SDK failed' }
}
$lib = "$SdkDir\lib\Windows_x86_64"
foreach ($p in "$SdkDir\include\nvsdk_ngx.h", "$lib\x64\nvsdk_ngx_d.lib", "$lib\rel\nvngx_dlss.dll", "$lib\rel\nvngx_dlssg.dll") { if (-not (Test-Path $p)) { throw "missing $p" } }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$obj = Join-Path ([IO.Path]::GetTempPath()) "kr-dlss-obj-$([guid]::NewGuid())"
New-Item -ItemType Directory $obj | Out-Null
& cl /nologo /std:c++17 /O2 /MD /EHsc /c "/I$SdkDir\include" "/I$env:VULKAN_SDK\Include" "/I$here" "/Fo$obj\plugin.obj" "$here\kr_upscaler_dlss.cpp"
if ($LASTEXITCODE) { throw 'compiling the DLSS plugin failed' }
& link /nologo /DLL "/OUT:$Out\kr_upscaler_dlss.dll" "$obj\plugin.obj" "$lib\x64\nvsdk_ngx_d.lib" advapi32.lib
if ($LASTEXITCODE) { throw 'linking kr_upscaler_dlss.dll failed' }
& cl /nologo /O2 /LD "$here\..\windows\forwarder.c" "/Fe:$Out\kr_framegen_dlss.dll" "/Fo$obj\forwarder.obj"
if ($LASTEXITCODE) { throw 'building the frame-generation forwarder failed' }
Copy-Item "$lib\rel\nvngx_dlss.dll", "$lib\rel\nvngx_dlssg.dll" $Out
Remove-Item -Recurse -Force $obj
Get-ChildItem $Out | ForEach-Object { Write-Host ("{0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
