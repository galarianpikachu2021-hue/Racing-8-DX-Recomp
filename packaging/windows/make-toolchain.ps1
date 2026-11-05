# SPDX-License-Identifier: GPL-3.0-or-later
# make-toolchain.ps1 <out dir>: assembles the compiler the Windows builder ships with: clang + lld + the mingw-w64 headers, CRT and
# import libraries from the official llvm-mingw release (LLVM licence with exceptions, mingw-w64 ZPL/public domain). MSVC and the
# Windows SDK cannot be redistributed, so the game modules are built for the mingw-w64 target with the UCRT. The download is
# checksum-pinned. Result: <out>/{bin/clang.exe, bin/ld.lld.exe, bin/lld.exe, bin/llvm-objdump.exe, x86_64-w64-mingw32, lib/clang, TOOLCHAIN_VERSION}.
param([Parameter(Mandatory = $true)][string]$Out)
$ErrorActionPreference = 'Stop'
$tag = '20261104'
$name = "llvm-mingw-$tag-ucrt-x86_64"
$sha = '552191b5754c49a5a20723276a6fc56541ea1922c4c50b7b669022333fc1b40f'
$tmp = Join-Path ([IO.Path]::GetTempPath()) "kr-toolchain-$([guid]::NewGuid())"
New-Item -ItemType Directory -Path $tmp | Out-Null
$zip = Join-Path $tmp "$name.zip"
Invoke-WebRequest -Uri "https://github.com/mstorsjo/llvm-mingw/releases/download/$tag/$name.zip" -OutFile $zip
if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower() -ne $sha) { throw "checksum mismatch for $name.zip" }
Expand-Archive -Path $zip -DestinationPath $tmp
$src = Join-Path $tmp $name
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Path "$Out\bin" | Out-Null
# Only what a C compile and a DLL link need.
foreach ($f in 'clang.exe', 'clang-*.exe', 'lld.exe', 'ld.lld.exe', 'llvm-objdump.exe', '*.dll') {
    Get-ChildItem "$src\bin" -Filter $f -ErrorAction SilentlyContinue | Copy-Item -Destination "$Out\bin"
}
# every target folder at the top (x86_64-w64-mingw32, and generic-w64-mingw32 which holds the shared mingw-w64 headers), nothing else
Get-ChildItem $src -Directory | Where-Object { $_.Name -like '*-w64-mingw32' } | ForEach-Object { Copy-Item -Recurse $_.FullName "$Out\$($_.Name)" }
Copy-Item -Recurse "$src\lib" "$Out\lib"
Remove-Item -Recurse -Force "$Out\lib\clang\*\lib\*" -Exclude 'windows' -ErrorAction SilentlyContinue
"llvm-mingw $tag (ucrt, x86_64)" | Set-Content "$Out\TOOLCHAIN_VERSION"
Remove-Item -Recurse -Force $tmp
$mb = [math]::Round((Get-ChildItem $Out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "toolchain: $mb MB in $Out"
