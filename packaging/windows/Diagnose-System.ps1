# SPDX-License-Identifier: GPL-3.0-or-later
# Diagnose-System.ps1 [-Out file]: writes the facts about this PC that matter for the game's speed and frame generation (no personal data, no file lists):
# Windows build, CPU (cores, hybrid P/E layout), RAM, GPUs and drivers, displays and refresh rate, power plan, HDR, game-mode, timer resolution.
param([string]$Out = 'system-info.txt')
$ErrorActionPreference = 'SilentlyContinue'
$o = New-Object System.Collections.Generic.List[string]
function Add($t) { $o.Add($t) }
Add "== system info $(Get-Date -Format s)"
$os = Get-CimInstance Win32_OperatingSystem
Add "Windows: $($os.Caption) $($os.Version) build $($os.BuildNumber)"
foreach ($c in Get-CimInstance Win32_Processor) { Add "CPU: $($c.Name.Trim()) | $($c.NumberOfCores) cores, $($c.NumberOfLogicalProcessors) threads, max $($c.MaxClockSpeed) MHz" }
Add ("RAM: {0:N1} GB total, {1:N1} GB free" -f ($os.TotalVisibleMemorySize / 1MB), ($os.FreePhysicalMemory / 1MB))
Add 'Cores by efficiency class (hybrid CPUs: the highest class is the performance cores):'
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class KrCpu {
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetLogicalProcessorInformationEx(int rel, IntPtr buf, ref uint len);
  public static string Classes() {
    uint len = 0; GetLogicalProcessorInformationEx(0, IntPtr.Zero, ref len); if (len == 0) return "unknown";
    IntPtr p = Marshal.AllocHGlobal((int)len); var counts = new SortedDictionary<int,int>();
    if (GetLogicalProcessorInformationEx(0, p, ref len)) { int off = 0; while (off < len) { int size = Marshal.ReadInt32(p, off + 4); int eff = Marshal.ReadByte(p, off + 9); int flags = Marshal.ReadByte(p, off + 8);
        int c; counts.TryGetValue(eff, out c); counts[eff] = c + 1; off += size; } }
    Marshal.FreeHGlobal(p); var sb = new System.Text.StringBuilder(); foreach (var kv in counts) sb.Append("class " + kv.Key + ": " + kv.Value + " cores; "); return sb.ToString(); }
}
"@
Add ("  " + [KrCpu]::Classes())
foreach ($g in Get-CimInstance Win32_VideoController) { Add "GPU: $($g.Name) | driver $($g.DriverVersion) ($($g.DriverDate)) | $($g.CurrentHorizontalResolution)x$($g.CurrentVerticalResolution) @ $($g.CurrentRefreshRate) Hz" }
$plan = (powercfg /getactivescheme) -join ' '
Add "Power plan: $plan"
$gm = Get-ItemProperty 'HKCU:\Software\Microsoft\GameBar' -Name AutoGameModeEnabled
Add "Game Mode: $($gm.AutoGameModeEnabled)"
Add ("Hardware-accelerated GPU scheduling: " + (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers' -Name HwSchMode).HwSchMode)
Add ("Vulkan runtime: " + ((Get-Item "$env:SystemRoot\System32\vulkan-1.dll").VersionInfo.FileVersion))
$o | Set-Content -Encoding UTF8 $Out
Write-Host "Wrote $Out"
