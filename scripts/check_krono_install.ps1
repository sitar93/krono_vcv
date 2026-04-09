param(
    [string]$RackUserDir = "$env:LOCALAPPDATA\Rack2",
    [switch]$RequireLogLoad
)

$ErrorActionPreference = "Stop"

$pluginDir = Join-Path $RackUserDir "plugins-win-x64\Krono"
$pluginJson = Join-Path $pluginDir "plugin.json"
$pluginDll = Join-Path $pluginDir "plugin.dll"
$logPath = Join-Path $RackUserDir "log.txt"

$ok = $true

Write-Host "[krono-check] Rack user dir: $RackUserDir"

if (!(Test-Path $pluginDir)) {
    Write-Host "[krono-check] MISSING plugin folder: $pluginDir" -ForegroundColor Red
    $ok = $false
} else {
    Write-Host "[krono-check] Found plugin folder: $pluginDir" -ForegroundColor Green
}

if (!(Test-Path $pluginJson)) {
    Write-Host "[krono-check] MISSING plugin.json: $pluginJson" -ForegroundColor Red
    $ok = $false
} else {
    Write-Host "[krono-check] Found plugin.json" -ForegroundColor Green
}

if (!(Test-Path $pluginDll)) {
    Write-Host "[krono-check] MISSING plugin.dll: $pluginDll" -ForegroundColor Red
    $ok = $false
} else {
    Write-Host "[krono-check] Found plugin.dll" -ForegroundColor Green
}

if (Test-Path $logPath) {
    $kronoLines = Select-String -Path $logPath -Pattern "Krono" -SimpleMatch
    $errorLines = Select-String -Path $logPath -Pattern "error|failed|exception" -CaseSensitive:$false
    if ($kronoLines) {
        Write-Host "[krono-check] Log mentions Krono (recent evidence found)." -ForegroundColor Green
    } else {
        Write-Host "[krono-check] Log has no Krono entries yet." -ForegroundColor Yellow
        if ($RequireLogLoad) {
            $ok = $false
        }
    }

    if ($errorLines) {
        Write-Host "[krono-check] Potential errors in Rack log (showing up to 8):" -ForegroundColor Yellow
        $errorLines | Select-Object -First 8 | ForEach-Object { Write-Host ("  " + $_.Line) }
    }
} else {
    Write-Host "[krono-check] Rack log not found yet: $logPath" -ForegroundColor Yellow
    if ($RequireLogLoad) {
        $ok = $false
    }
}

if (!$ok) {
    Write-Host "[krono-check] FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "[krono-check] OK" -ForegroundColor Green
exit 0
