# SPDX-License-Identifier: GPL-3.0-or-later
# test_release_contents.ps1 <folder> [-RequirePlugins]: the folder holds what a user needs and nothing derived from a game or from NVIDIA.
param([Parameter(Mandatory = $true)][string]$Dir, [switch]$RequirePlugins)
$d = (Resolve-Path $Dir).Path
$fail = @()
foreach ($f in 'kartrecomp.exe', 'kartrecomp-setup.exe', 'kartrecomp-build.exe', 'engine\mk8d-engine.exe', 'engine\ENGINE_VERSION', 'tools\kartrecomp-export.exe',
               'toolchain\bin\clang.exe', 'toolchain\bin\ld.lld.exe', 'data\mk8d-3.0.4\hot_blocks.txt', 'README-WINDOWS.txt', 'Collect-Logs.cmd') {
    if (-not (Test-Path "$d\$f")) { $fail += "missing $f" }
}
if ($RequirePlugins -and -not (Get-ChildItem "$d\engine\plugins" -Filter 'kr_upscaler_fsr3*.dll' -ErrorAction SilentlyContinue)) { $fail += 'no FSR 3 plugin' }
$bad = Get-ChildItem $d -Recurse -File | Where-Object { $_.Extension -in '.nsp', '.nca', '.keys', '.nso', '.xci', '.nsz' -or $_.Name -like 'recompiled_*' -or $_.Name -like 'nvngx*' -or $_.Name -like '*dlss*' -or $_.Name -eq 'prod.keys' }
foreach ($b in $bad) { $fail += "must not ship: $($b.FullName.Substring($d.Length))" }
if ((Test-Path "$d\userdata")) { $fail += 'userdata folder must not be shipped' }
# every exe and dll resolves its imports against the folder or the system
$objdump = "$d\toolchain\bin\llvm-objdump.exe"
if (Test-Path $objdump) {
    $have = @{}
    Get-ChildItem $d -Recurse -Filter *.dll | ForEach-Object { $have[$_.Name.ToLower()] = $true }
    foreach ($exe in 'kartrecomp.exe', 'engine\mk8d-engine.exe', 'tools\kartrecomp-export.exe') {
        $dir = Split-Path "$d\$exe"
        $own = @{}; Get-ChildItem $dir -Filter *.dll | ForEach-Object { $own[$_.Name.ToLower()] = $true }
        foreach ($dll in (& $objdump -p "$d\$exe" | Select-String 'DLL Name:' | ForEach-Object { ($_ -split ':')[1].Trim().ToLower() })) {
            $system = Test-Path "$env:SystemRoot\System32\$dll"
            if ($dll -ne (Split-Path $exe -Leaf).ToLower() -and -not $own.ContainsKey($dll) -and -not $system -and $dll -notlike 'api-ms-win-*') { $fail += "$exe needs $dll, which is neither beside it nor a Windows DLL" }
        }
    }
}
if ($fail) { $fail | ForEach-Object { Write-Host "FAIL: $_" }; exit 1 }
Write-Host 'PASS'
exit 0
