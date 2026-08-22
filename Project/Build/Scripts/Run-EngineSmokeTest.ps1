<#
.SYNOPSIS
    Launches the engine, lets it render, closes it, and reports a PASS/FAIL verdict.

.DESCRIPTION
    Regression harness for the DX12 foundation refactoring
    (see Docs/Engine/Graphics/Common/DX12_Foundation_Refactoring_Review.md).

    In a Debug build the D3D12 debug layer runs with break-on-severity for
    CORRUPTION / ERROR / WARNING, so "the process survived and exited with 0"
    IS the debug-layer-clean check. On top of that this script scans the logs
    written by this run for [error] / [warning], reports the descriptor-heap
    peak (leak / exhaustion indicator) and detects new minidumps.

    Run it before and after every phase.

.PARAMETER Config
    Debug (default) / Development / Release. Use Debug for the debug layer.

.PARAMETER SettleSec
    Seconds to keep rendering after the main window appears. 45 s is the
    minimum that reliably produces a fully-loaded frame in the capture.

.PARAMETER ShotPath
    Where to write the PrintWindow capture. Empty string disables capture.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -NoProfile -File Build\Scripts\Run-EngineSmokeTest.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Development', 'Release')]
    [string]$Config    = 'Debug',
    [int]   $SettleSec = 45,
    [string]$ShotPath  = '',
    [string]$Root      = 'C:\CoreEngine\Project'
)

$ErrorActionPreference = 'Stop'
$exe = "C:\CoreEngine\generated\CoreEngine\outputs\$Config\CoreEngine.exe"
if (-not (Test-Path $exe)) { throw "exe not found: $exe  (build $Config first)" }
if (-not $ShotPath) { $ShotPath = Join-Path $env:TEMP "coreengine_smoke_$Config.png" }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SmokeWin {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    // PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE
    public const uint ACCESS = 0x1000 | 0x00100000;
}
"@

$fail = @()
$start = Get-Date
$dumpDir = Join-Path $Root 'Dumps'
$dumpsBefore = @()
if (Test-Path $dumpDir) { $dumpsBefore = @(Get-ChildItem $dumpDir -File | Select-Object -ExpandProperty Name) }

Write-Host "=== Engine smoke test ($Config) ==="
Write-Host "Launching $exe (cwd=$Root)"

# Detached launch: a normally-started child dies with the tool shell.
$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
    CommandLine      = $exe
    CurrentDirectory = $Root
}
if ($r.ReturnValue -ne 0) { throw "Win32_Process.Create failed: $($r.ReturnValue)" }
$procId = [int]$r.ProcessId
Write-Host "  pid = $procId"

# Our own handle: Process.ExitCode is unavailable for processes we did not start.
$hProc = [SmokeWin]::OpenProcess([SmokeWin]::ACCESS, $false, $procId)
function Get-RealExitCode {
    if ($hProc -eq [IntPtr]::Zero) { return '<unavailable>' }
    $code = 0
    if ([SmokeWin]::GetExitCodeProcess($hProc, [ref]$code)) {
        if ($code -eq 259) { return '<still running>' }   # STILL_ACTIVE
        return [int]$code
    }
    return '<query failed>'
}

# --- wait for the real main window (the splash window is smaller) ---
$proc = $null
$hwnd = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(150)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 700
    $proc = Get-Process -Id $procId -ErrorAction SilentlyContinue
    if (-not $proc) { break }
    $proc.Refresh()
    $h = $proc.MainWindowHandle
    if ($h -ne [IntPtr]::Zero -and [SmokeWin]::IsWindowVisible($h)) {
        $rc = New-Object SmokeWin+RECT
        [void][SmokeWin]::GetClientRect($h, [ref]$rc)
        if (($rc.R - $rc.L) -ge 1200 -and ($rc.B - $rc.T) -ge 700) { $hwnd = $h; break }
    }
}

$exitCode = $null
if (-not $proc -or $proc.HasExited) {
    $exitCode = Get-RealExitCode
    $fail += "process exited during startup (ExitCode=$exitCode)"
} else {
    if ($hwnd -eq [IntPtr]::Zero) {
        $fail += "main window did not appear within 150 s"
    } else {
        Write-Host ("  main window up after {0:N1} s" -f ((Get-Date) - $start).TotalSeconds)
    }

    Write-Host "  rendering for $SettleSec s ..."
    Start-Sleep -Seconds $SettleSec
    $proc.Refresh()

    if ($proc.HasExited) {
        $exitCode = Get-RealExitCode
        $fail += "process died while rendering (ExitCode=$exitCode) - likely a debug-layer break"
    } else {
        # --- capture: PrintWindow needs no foreground, so nothing else on screen is captured ---
        if ($hwnd -ne [IntPtr]::Zero) {
            $rc = New-Object SmokeWin+RECT
            [void][SmokeWin]::GetClientRect($hwnd, [ref]$rc)
            $w = $rc.R - $rc.L; $h2 = $rc.B - $rc.T
            $bmp = New-Object System.Drawing.Bitmap($w, $h2)
            $g   = [System.Drawing.Graphics]::FromImage($bmp)
            $dc  = $g.GetHdc()
            $ok  = [SmokeWin]::PrintWindow($hwnd, $dc, 3)
            $g.ReleaseHdc($dc); $g.Dispose()
            if ($ok) {
                $bmp.Save($ShotPath, [System.Drawing.Imaging.ImageFormat]::Png)
                Write-Host "  screenshot: $ShotPath ($w x $h2)"
            } else { Write-Host "  screenshot: PrintWindow failed (not fatal)" }
            $bmp.Dispose()
        }

        Write-Host "  closing ..."
        [void]$proc.CloseMainWindow()
        if (-not $proc.WaitForExit(40000)) {
            $proc.Kill(); $proc.WaitForExit(10000)
            $exitCode = Get-RealExitCode
            $fail += "did not exit within 40 s of CloseMainWindow (killed)"
        } else {
            $exitCode = Get-RealExitCode
            if ($exitCode -ne 0) { $fail += "ExitCode=$exitCode" }
        }
    }
}

# ------------------------------------------------------------------
# Log analysis (only files written by this run)
# ------------------------------------------------------------------
Write-Host ""
Write-Host "--- logs ---"
$logRoot = Join-Path $Root 'Cache\logs'
$errTotal = 0
$warnLines = @()
$heapPeak = 0.0
$maxSrvIndex = -1

if (Test-Path $logRoot) {
    $runLogs = Get-ChildItem $logRoot -Recurse -Filter *.log -File |
               Where-Object { $_.LastWriteTime -ge $start -and $_.Length -gt 0 }
    foreach ($lf in $runLogs) {
        $text = [System.IO.File]::ReadAllText($lf.FullName)
        $e = ([regex]::Matches($text, '\[(?:error|critical)\]')).Count
        $errTotal += $e
        foreach ($m in [regex]::Matches($text, '(?m)^.*\[warning\].*$')) { $warnLines += $m.Value.Trim() }
        if ($lf.Name -like 'Heap_*') {
            foreach ($m in [regex]::Matches($text, '(\d+\.\d+)%')) {
                $v = [double]$m.Groups[1].Value
                if ($v -gt $heapPeak) { $heapPeak = $v }
            }
            foreach ($m in [regex]::Matches($text, 'SRV/CBV/UAV\[(\d+)\]')) {
                $v = [int]$m.Groups[1].Value
                if ($v -gt $maxSrvIndex) { $maxSrvIndex = $v }
            }
        }
        Write-Host ("  {0,-46} {1,7} B  err={2}" -f $lf.Name, $lf.Length, $e)
    }
}
if ($errTotal -gt 0) { $fail += "$errTotal error/critical log entries" }

Write-Host ""
Write-Host "--- warnings ($($warnLines.Count)) ---"
foreach ($w in ($warnLines | Select-Object -First 12)) { Write-Host "  $w" }
if ($warnLines.Count -gt 12) { Write-Host "  ... $($warnLines.Count - 12) more" }

Write-Host ""
Write-Host "--- descriptor heap ---"
Write-Host ("  peak SRV/CBV/UAV usage : {0}%" -f $heapPeak)
Write-Host ("  highest SRV slot index : {0}" -f $maxSrvIndex)

# ------------------------------------------------------------------
# Minidumps
# ------------------------------------------------------------------
$newDumps = @()
if (Test-Path $dumpDir) {
    $newDumps = @(Get-ChildItem $dumpDir -File | Where-Object { $dumpsBefore -notcontains $_.Name })
}
if ($newDumps.Count -gt 0) {
    $fail += "$($newDumps.Count) new minidump(s)"
    Write-Host ""
    Write-Host "--- NEW MINIDUMPS ---"
    foreach ($d in $newDumps) { Write-Host "  $($d.FullName)" }
}

# ------------------------------------------------------------------
# Verdict
# ------------------------------------------------------------------
$total = [int]((Get-Date) - $start).TotalSeconds
Write-Host ""
Write-Host "=================================================="
if ($fail.Count -eq 0) {
    Write-Host "PASS  ExitCode=$exitCode  errors=0  warnings=$($warnLines.Count)  peakSRV=$heapPeak%  ($total s)"
    Write-Host "=================================================="
    exit 0
} else {
    Write-Host "FAIL  ($total s)"
    foreach ($f in $fail) { Write-Host "  - $f" }
    Write-Host "=================================================="
    exit 1
}
