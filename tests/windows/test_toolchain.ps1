# SPDX-License-Identifier: GPL-3.0-or-later
# test_toolchain.ps1 <toolchain dir>: the bundled toolchain compiles and links a module-style DLL with NO other compiler on PATH; the DLL
# exports recomp_image_abi through __declspec(dllexport) like the generated sources, needs only system DLLs, and loads and runs.
param([Parameter(Mandatory = $true)][string]$Toolchain)
$ErrorActionPreference = 'Continue'
$T = (Resolve-Path $Toolchain).Path
$clang = "$T\bin\clang.exe"
if (-not (Test-Path $clang)) { Write-Host "FAIL: no clang in $T\bin"; exit 1 }
$tmp = Join-Path ([IO.Path]::GetTempPath()) "kr-tc-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $tmp | Out-Null
@'
#include <math.h>
#include <stdint.h>
#include <string.h>
#define RECOMP_API __declspec(dllexport)
static double hidden_helper(double x) { char b[8]; uint64_t v = 7; memcpy(b, &v, 8); return fmod(x, 3.0) + sqrt(x) + (double)b[0]; }
RECOMP_API unsigned recomp_image_abi(void) { return 6; }
RECOMP_API double module_probe(double x) { return hidden_helper(x); }
RECOMP_API __int128 wide(uint64_t a, uint64_t b) { return (__int128)a * b; }
'@ | Set-Content "$tmp\m.c"
@'
#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("load failed %lu\n", GetLastError()); return 2; }
    unsigned (*abi)(void) = (unsigned (*)(void))GetProcAddress(h, "recomp_image_abi");
    double (*probe)(double) = (double (*)(double))GetProcAddress(h, "module_probe");
    if (!abi || !probe) { printf("missing export\n"); return 3; }
    printf("%u %.3f\n", abi(), probe(10.0));
    return 0;
}
'@ | Set-Content "$tmp\host.c"
# a PATH with no compiler at all
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$flags = '--target=x86_64-w64-mingw32', '-std=gnu11', '-O2', '-march=x86-64-v3', '-fvisibility=hidden'
& $clang @flags -c "$tmp\m.c" -o "$tmp\m.o"
if ($LASTEXITCODE -ne 0) {
    Write-Host 'FAIL: compile'
    Get-ChildItem $T | Select-Object -ExpandProperty Name | Write-Host
    Get-ChildItem "$T\x86_64-w64-mingw32" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name | Write-Host
    & $clang @flags -v -c "$tmp\m.c" -o "$tmp\m.o" 2>&1 | Write-Host
    exit 1
}
& $clang --target=x86_64-w64-mingw32 -fuse-ld=lld -rtlib=compiler-rt -unwindlib=none -shared -o "$tmp\recompiled_image.dll" "$tmp\m.o" -lm
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL: link'; exit 1 }
& $clang --target=x86_64-w64-mingw32 -fuse-ld=lld -rtlib=compiler-rt -unwindlib=none -o "$tmp\host.exe" "$tmp\host.c"
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL: host build'; exit 1 }
$imports = & "$T\bin\llvm-objdump.exe" -p "$tmp\recompiled_image.dll" | Select-String 'DLL Name:' | ForEach-Object { ($_ -split ':')[1].Trim().ToLower() }
$allowed = 'kernel32.dll', 'msvcrt.dll', 'ucrtbase.dll', 'recompiled_image.dll' # the last is the export table naming itself
$extra = $imports | Where-Object { ($_ -notin $allowed) -and ($_ -notlike 'api-ms-win-crt-*') }
if ($extra) { Write-Host "FAIL: the module needs more than the system DLLs: $($extra -join ', ')"; exit 1 }
$out = & "$tmp\host.exe" "$tmp\recompiled_image.dll"
if ($out -ne '6 11.162') { Write-Host "FAIL: wrong result '$out' (wanted '6 11.162')"; exit 1 }
Remove-Item -Recurse -Force $tmp
Write-Host 'PASS'
exit 0
