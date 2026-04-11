param(
    [string]$RackExe = "$env:ProgramFiles\VCV\Rack2Pro\Rack.exe",
    [switch]$WaitForExit,
    [switch]$CheckAfterExit,
    [switch]$DismissCacheDialog
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "vcv_rack_dialog.ps1")
. (Join-Path $PSScriptRoot "vcv_rack_paths.ps1")

if (!(Test-Path $RackExe)) {
    $fallbacks = @(
        "$env:ProgramFiles\VCV\Rack2Free\Rack.exe",
        "${env:ProgramFiles(x86)}\VCV\Rack2Pro\Rack.exe",
        "${env:ProgramFiles(x86)}\VCV\Rack2Free\Rack.exe"
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            $RackExe = $candidate
            break
        }
    }
}

if (!(Test-Path $RackExe)) {
    throw "Rack executable not found. Pass it explicitly with -RackExe."
}

$userDir = Get-VcvRackUserDir -RackExe $RackExe
Write-Host "[krono] Starting Rack: $RackExe"
Write-Host "[krono] Rack user dir (plugins, settings): $userDir"
if ($WaitForExit) {
    $proc = Start-Process -FilePath $RackExe -PassThru
    if ($DismissCacheDialog) {
        Start-Sleep -Seconds 2
        Invoke-VcvDismissCacheDialog -Attempts 2
    }
    $proc.WaitForExit()
    if ($CheckAfterExit) {
        $checkScript = Join-Path $PSScriptRoot "check_krono_install.ps1"
        if (Test-Path $checkScript) {
            & $checkScript -RequireLogLoad -RackExe $RackExe
        }
    }
} else {
    Start-Process -FilePath $RackExe
    if ($DismissCacheDialog) {
        Start-Sleep -Seconds 2
        Invoke-VcvDismissCacheDialog -Attempts 2
    }
}
