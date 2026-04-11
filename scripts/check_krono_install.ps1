param(
    [string]$RackUserDir = "",
    [string]$RackExe = "",
    [switch]$RequireLogLoad
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "vcv_rack_paths.ps1")

if (!$RackUserDir -and $env:RACK_USER_DIR) {
    $RackUserDir = $env:RACK_USER_DIR
}

if (!$RackExe) {
    $RackExe = Get-VcvRackExe
}

$RackUserDir = Get-VcvRackUserDir -RackExe $RackExe -OverrideUserDir $RackUserDir

$pluginsRoot = Join-Path $RackUserDir "plugins-win-x64"
$pluginDir = Join-Path $pluginsRoot "Krono"
$pluginJson = Join-Path $pluginDir "plugin.json"
$pluginDll = Join-Path $pluginDir "plugin.dll"
$logPath = Join-Path $RackUserDir "log.txt"

$ok = $true
# Brackets in "[...]" break PowerShell parsing inside double quotes; use a prefix variable.
$chk = '[krono-check]'

Write-Host "$chk Rack user dir: $RackUserDir"
Write-Host "$chk plugins-win-x64: $pluginsRoot"

$slug = "Krono"
$packages = @(Get-ChildItem -LiteralPath $pluginsRoot -Filter "$slug*.vcvplugin" -ErrorAction SilentlyContinue)
$extracted = (Test-Path -LiteralPath $pluginDll)

if ($packages.Count -gt 0) {
    Write-Host "$chk Found plugin package(s): $($packages.Name -join ', ')" -ForegroundColor Green
}
if ($extracted) {
    Write-Host "$chk Found extracted folder: $pluginDir" -ForegroundColor Green
}

if ($packages.Count -eq 0 -and !$extracted) {
    Write-Host "$chk MISSING: no ${slug}*.vcvplugin and no $pluginDir\plugin.dll" -ForegroundColor Red
    $ok = $false
}

if ($packages.Count -gt 0 -and !$extracted) {
    Write-Host "$chk Note: only .vcvplugin present - Rack extracts on first launch; open Rack once then rescan." -ForegroundColor Yellow
}

if ($extracted) {
    if (!(Test-Path -LiteralPath $pluginJson)) {
        Write-Host "$chk MISSING plugin.json: $pluginJson" -ForegroundColor Red
        $ok = $false
    } else {
        Write-Host "$chk Found plugin.json" -ForegroundColor Green
    }
    if (!(Test-Path -LiteralPath $pluginDll)) {
        Write-Host "$chk MISSING plugin.dll: $pluginDll" -ForegroundColor Red
        $ok = $false
    } else {
        Write-Host "$chk Found plugin.dll" -ForegroundColor Green
    }
}

if (Test-Path -LiteralPath $logPath) {
    $kronoLines = Select-String -Path $logPath -Pattern "Krono" -SimpleMatch -ErrorAction SilentlyContinue
    $failLines = Select-String -Path $logPath -Pattern "Krono|plugin\.json|Failed to load|Could not load|Skipping plugin|Invalid plugin" -CaseSensitive:$false -ErrorAction SilentlyContinue |
        Select-Object -Last 12
    if ($kronoLines) {
        Write-Host "$chk Log mentions Krono." -ForegroundColor Green
    } else {
        Write-Host "$chk Log has no 'Krono' substring yet (normal before first Rack run)." -ForegroundColor Yellow
        if ($RequireLogLoad) {
            $ok = $false
        }
    }
    if ($failLines) {
        Write-Host "$chk Recent log lines (plugin/load):" -ForegroundColor Cyan
        $failLines | ForEach-Object { Write-Host ("  " + $_.Line) }
    }
} else {
    Write-Host "$chk Rack log not found yet: $logPath" -ForegroundColor Yellow
    if ($RequireLogLoad) {
        $ok = $false
    }
}

if (!$ok) {
    Write-Host "$chk FAILED" -ForegroundColor Red
    Write-Host "$chk Tips: Library - Rescan. plugin.json version MAJOR must be 2 for Rack 2 (see VCV Manifest). Portable Rack: set RACK_USER_DIR." -ForegroundColor Yellow
    exit 1
}

Write-Host "$chk OK" -ForegroundColor Green
exit 0
