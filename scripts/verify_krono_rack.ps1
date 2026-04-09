# Automated smoke test: build, install, launch Rack, open module browser, search "Krono"
# to trigger createPreview (same path as manual crash). Fails if Rack exits or log shows fatal near Krono.
param(
    [string]$RackDir = "D:\Files\VCV\Rack-SDK",
    [string]$RackExe = "",
    [switch]$SkipBuild,
    [int]$WaitAfterSearchSec = 12,
    [switch]$DismissCacheDialog
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "vcv_rack_dialog.ps1")

function Find-RackExe {
    $candidates = @(
        "$env:ProgramFiles\VCV\Rack2Free\Rack.exe",
        "$env:ProgramFiles\VCV\Rack2Pro\Rack.exe",
        "${env:ProgramFiles(x86)}\VCV\Rack2Free\Rack.exe",
        "${env:ProgramFiles(x86)}\VCV\Rack2Pro\Rack.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$logPath = Join-Path $env:LOCALAPPDATA "Rack2\log.txt"

if ([string]::IsNullOrEmpty($RackExe)) {
    $RackExe = Find-RackExe
}
if (-not $RackExe -or -not (Test-Path $RackExe)) {
    throw "Rack.exe not found. Install VCV Rack 2 or pass -RackExe."
}

if (-not $SkipBuild) {
    Write-Host "[verify] Build + install..."
    & (Join-Path $PSScriptRoot "build_windows.ps1") -RackDir $RackDir -InstallToRack
}

Write-Host "[verify] Stopping any running Rack..."
Get-Process -Name "Rack" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

Write-Host "[verify] Starting Rack: $RackExe"
$proc = Start-Process -FilePath $RackExe -PassThru
$rackProc = $null

# Wait until Rack process exists (Start-Process can race).
$aliveDeadline = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $aliveDeadline) {
    $rackProc = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($rackProc) { break }
    Start-Sleep -Milliseconds 100
}
if (-not $rackProc) {
    if (Test-Path $logPath) { Get-Content $logPath -Tail 40 | ForEach-Object { Write-Host $_ } }
    Write-Error "[verify] Rack process ended immediately (startup crash or blocked). Check $logPath"
    exit 2
}

if ($DismissCacheDialog) {
    Write-Host "[verify] Dismissing startup cache dialog (optional)..."
    Start-Sleep -Seconds 2
    Invoke-VcvDismissCacheDialog -Attempts 2
}

$deadline = (Get-Date).AddSeconds(90)
$rackProc = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
while ((Get-Date) -lt $deadline) {
    $rackProc = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if (-not $rackProc) {
        if (Test-Path $logPath) { Get-Content $logPath -Tail 60 | ForEach-Object { Write-Host $_ } }
        Write-Error "[verify] Rack exited while waiting for main window. Check $logPath"
        exit 2
    }
    if ($rackProc.MainWindowHandle -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 400
}

if (-not $rackProc -or $rackProc.MainWindowHandle -eq [IntPtr]::Zero) {
    try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    throw "[verify] Rack window did not appear in time."
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class KronoVerifyWin32 {
  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@

Write-Host "[verify] Focusing Rack and opening module browser (Enter)..."
[void][KronoVerifyWin32]::SetForegroundWindow($rackProc.MainWindowHandle)
Start-Sleep -Milliseconds 600

Add-Type -AssemblyName System.Windows.Forms
# Open module browser (default shortcut Enter when rack focused)
[System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
Start-Sleep -Seconds 2

Write-Host "[verify] Typing search: Krono (triggers browser preview)..."
[System.Windows.Forms.SendKeys]::SendWait("Krono")
Start-Sleep -Seconds $WaitAfterSearchSec

if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) {
    Write-Error "[verify] Rack process terminated during test (likely crash)."
    if (Test-Path $logPath) {
        Get-Content $logPath -Tail 80 | ForEach-Object { Write-Host $_ }
    }
    exit 3
}

if (Test-Path $logPath) {
    $tail = Get-Content $logPath -Tail 120
    $fatalLines = $tail | Where-Object { $_ -match '\bfatal\b' }
    $previewKrono = $tail | Where-Object { $_ -match 'createPreview.*Krono|Creating module widget Krono' }
    if ($fatalLines) {
        Write-Host "[verify] --- log (fatal lines) ---"
        $fatalLines | ForEach-Object { Write-Host $_ }
        if ($previewKrono) {
            Write-Host "[verify] --- Krono preview was requested; fatal likely from this plugin ---"
        }
        try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        exit 4
    }
}

Write-Host "[verify] Stopping Rack..."
try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}

Write-Host "[verify] OK (no fatal in recent log; Rack still running until stopped)."
exit 0
