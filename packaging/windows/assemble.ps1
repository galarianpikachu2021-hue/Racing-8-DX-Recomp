# SPDX-License-Identifier: GPL-3.0-or-later
# assemble.ps1 -Engine <dir> -Launcher <dir> -Toolchain <dir> -Out <dir>: lays out the Windows folder that is zipped for users.
#   kartrecomp.exe (+ Qt DLLs), kartrecomp-setup.exe, kartrecomp-build.exe
#   engine\   mk8d-engine.exe, its DLLs, ENGINE_VERSION, plugins\
#   tools\    kartrecomp-export.exe (+ the same DLLs)
#   toolchain\  clang + lld + mingw-w64 (builds the game modules on the user's PC)
#   data\mk8d-<update>\hot_blocks.txt (one per supported update)
# Never game files: nothing is read from outside the three inputs and the repository.
param(
    [Parameter(Mandatory = $true)][string]$Engine,
    [Parameter(Mandatory = $true)][string]$Launcher,
    [Parameter(Mandatory = $true)][string]$Toolchain,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Plugins = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Path "$Out\engine\plugins", "$Out\tools", "$Out\data\mk8d-3.0.4" | Out-Null
Copy-Item "$Launcher\*" $Out -Recurse
Copy-Item "$Engine\mk8d-engine.exe", "$Engine\ENGINE_VERSION" "$Out\engine"
Copy-Item "$Engine\kartrecomp-export.exe" "$Out\tools"
if (Test-Path "$Engine\mimalloc-LICENSE.txt") { Copy-Item "$Engine\mimalloc-LICENSE.txt" "$Out\engine" }
Get-ChildItem $Engine -Filter *.dll | ForEach-Object { Copy-Item $_.FullName "$Out\engine"; Copy-Item $_.FullName "$Out\tools" }
if ($Plugins -and (Test-Path $Plugins)) { Copy-Item "$Plugins\*" "$Out\engine\plugins" -Recurse }
Copy-Item $Toolchain "$Out\toolchain" -Recurse
Get-ChildItem "$repo\data" -Directory -Filter "mk8d-*" | ForEach-Object { New-Item -ItemType Directory -Force -Path "$Out\data\$($_.Name)" | Out-Null; Copy-Item "$($_.FullName)\hot_blocks.txt" "$Out\data\$($_.Name)\" }
foreach ($f in 'LEGAL.md', 'LICENSE') { if (Test-Path "$repo\$f") { Copy-Item "$repo\$f" $Out } }
Copy-Item "$repo\packaging\windows\README-WINDOWS.txt", "$repo\packaging\windows\Collect-Logs.cmd", "$repo\packaging\windows\Diagnose-System.ps1" $Out
Write-Host "assembled $Out"
