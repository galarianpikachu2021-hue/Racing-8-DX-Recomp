# SPDX-License-Identifier: GPL-3.0-or-later
# test_plugins.ps1 <plugins dir> [-Expect fsr3,fsr4,dlss]: every kr_upscaler_<id>.dll in the folder loads, exports kr_upscaler_get_api with ABI version 1,
# returns NULL from create() when there is no GPU instance (instead of crashing), and the optional model exports (kr_upscaler_models) are present for FSR 4 and
# DLSS. Libraries that also do frame generation have a kr_framegen_<id>.dll forwarder that resolves to the SAME module (same api pointer as the upscaler's).
# Needs no GPU. The real behaviour on a GPU is checked by hand (see docs/WINDOWS_PLUGINS.md).
param([Parameter(Mandatory = $true)][string]$Dir, [string[]]$Expect = @('fsr3', 'fsr4'))
$ErrorActionPreference = 'Stop'
$d = (Resolve-Path $Dir).Path
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class KrProbe {
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)] public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32", CharSet = CharSet.Ansi)] public static extern IntPtr GetProcAddress(IntPtr module, string name);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate IntPtr GetApi();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate IntPtr Create(IntPtr device);
    public static IntPtr CallGetApi(IntPtr fn) { return Marshal.GetDelegateForFunctionPointer<GetApi>(fn)(); }
    public static IntPtr CallCreate(IntPtr fn, IntPtr dev) { return Marshal.GetDelegateForFunctionPointer<Create>(fn)(dev); }
}
"@
$fail = @()
function Load($path) { [KrProbe]::LoadLibraryExW($path, [IntPtr]::Zero, 8) }   # LOAD_WITH_ALTERED_SEARCH_PATH: dependencies from the plugin's folder
foreach ($id in $Expect) {
    $up = "$d\kr_upscaler_$id.dll"
    if (-not (Test-Path $up)) { $fail += "missing kr_upscaler_$id.dll"; continue }
    $m = Load $up
    if ($m -eq [IntPtr]::Zero) { $fail += "kr_upscaler_$id.dll does not load (Windows error $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"; continue }
    $get = [KrProbe]::GetProcAddress($m, 'kr_upscaler_get_api')
    if ($get -eq [IntPtr]::Zero) { $fail += "${id}: no kr_upscaler_get_api"; continue }
    $api = [KrProbe]::CallGetApi($get)
    if ($api -eq [IntPtr]::Zero) { $fail += "${id}: kr_upscaler_get_api returned NULL"; continue }
    $abi = [Runtime.InteropServices.Marshal]::ReadInt32($api, 0)
    if ($abi -ne 1) { $fail += "${id}: ABI version $abi, expected 1" }
    $createPtr = [Runtime.InteropServices.Marshal]::ReadIntPtr($api, 16)   # { u32 abi; (pad) const char* name; create; ... }
    $zero = [Runtime.InteropServices.Marshal]::AllocHGlobal(256)
    0..31 | ForEach-Object { [Runtime.InteropServices.Marshal]::WriteInt64($zero, $_ * 8, 0) }
    if ([KrProbe]::CallCreate($createPtr, $zero) -ne [IntPtr]::Zero) { $fail += "${id}: create() with no GPU instance must return NULL" }
    if ($id -in 'fsr4', 'dlss' -and [KrProbe]::GetProcAddress($m, 'kr_upscaler_models') -eq [IntPtr]::Zero) { $fail += "${id}: no kr_upscaler_models (the menu's Model row)" }
    $fg = "$d\kr_framegen_$id.dll"
    if (Test-Path $fg) {
        $f = Load $fg
        $fget = if ($f -ne [IntPtr]::Zero) { [KrProbe]::GetProcAddress($f, 'kr_framegen_get_api') } else { [IntPtr]::Zero }
        if ($fget -eq [IntPtr]::Zero) { $fail += "kr_framegen_$id.dll does not load or lacks kr_framegen_get_api"; continue }
        $a = [KrProbe]::CallGetApi($fget)
        $direct = [KrProbe]::GetProcAddress($m, 'kr_framegen_get_api')
        $b = if ($direct -ne [IntPtr]::Zero) { [KrProbe]::CallGetApi($direct) } else { [IntPtr]::Zero }
        if ($a -eq [IntPtr]::Zero -or $a -ne $b) { $fail += "kr_framegen_$id.dll does not forward to the upscaler module" }
    }
    Write-Host "ok: $id (ABI $abi)"
}
if ($fail) { $fail | ForEach-Object { Write-Host "FAIL: $_" }; exit 1 }
Write-Host 'PASS'
exit 0
