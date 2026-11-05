# SPDX-License-Identifier: GPL-3.0-or-later
# test_module_guard.ps1 <engine dir>: the Windows twin of tests/test_module_guard.sh. The engine refuses to start without a built game (exit 5)
# and refuses modules built for another engine version or ABI (exit 4), with the reason on stderr. Needs clang on PATH for the stub module.
param([string]$EngineDir = "engine")
$ErrorActionPreference = "Stop"
$engine = Join-Path $EngineDir "mk8d-engine.exe"
if (-not (Test-Path $engine)) { Write-Host "FAIL: no engine at $engine"; exit 1 }
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("guard-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
function Expect($code, $fragment, $what, [string[]]$arguments) {
    $out = & $engine @arguments 2>&1 | Out-String
    if ($LASTEXITCODE -ne $code -or $out -notmatch [regex]::Escape($fragment)) {
        Write-Host "FAIL: $what (exit $LASTEXITCODE, wanted $code with '$fragment'): $out"; exit 1
    }
}
Expect 5 "No game has been built yet" "no module folder" @()
New-Item -ItemType Directory -Path "$tmp\empty" | Out-Null
Expect 5 "No game has been built yet" "empty module folder" @("--module-dir", "$tmp\empty")
New-Item -ItemType Directory -Path "$tmp\badabi" | Out-Null
@'
__declspec(dllexport) unsigned recomp_image_abi(void) { return 99; }
__declspec(dllexport) void* recomp_image_lookup(unsigned long long pc) { (void)pc; return 0; }
__declspec(dllexport) void recomp_image_set_base(unsigned long long b) { (void)b; }
'@ | Set-Content "$tmp\stub.c"
& clang --target=x86_64-pc-windows-msvc -shared -o "$tmp\badabi\recompiled_image.dll" "$tmp\stub.c"
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: cannot build the stub module"; exit 1 }
Expect 4 "ABI 99" "module with an unsupported ABI" @("--module-dir", "$tmp\badabi")
New-Item -ItemType Directory -Path "$tmp\otherengine" | Out-Null
Copy-Item "$tmp\badabi\recompiled_image.dll" "$tmp\otherengine\"
'{ "engineVersion": "some-other-engine" }' | Set-Content "$tmp\otherengine\module.json"
Expect 4 "some-other-engine" "modules built for another engine version" @("--module-dir", "$tmp\otherengine")
Remove-Item -Recurse -Force $tmp
Write-Host "PASS"

exit 0
