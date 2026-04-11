# Shared helpers: Rack.exe discovery and user directory (standard vs portable).

function Get-VcvRackExeCandidates {
    return @(
        "$env:ProgramFiles\VCV\Rack2Pro\Rack.exe",
        "$env:ProgramFiles\VCV\Rack2Free\Rack.exe",
        "${env:ProgramFiles(x86)}\VCV\Rack2Pro\Rack.exe",
        "${env:ProgramFiles(x86)}\VCV\Rack2Free\Rack.exe"
    )
}

<#
.SYNOPSIS
  Resolve path to Rack.exe (optional explicit path, else first existing candidate).
#>
function Get-VcvRackExe {
    param([string]$RackExe)
    if ($RackExe -and (Test-Path -LiteralPath $RackExe)) {
        return (Resolve-Path -LiteralPath $RackExe).Path
    }
    foreach ($c in Get-VcvRackExeCandidates) {
        if (Test-Path -LiteralPath $c) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    return $null
}

<#
.SYNOPSIS
  Rack 2 user folder: if Rack2 exists next to Rack.exe (portable), use it; else %LOCALAPPDATA%\Rack2.
  See https://vcvrack.com/manual/Installing#portable-mode
#>
function Get-VcvRackUserDir {
    param(
        [string]$RackExe,
        [string]$OverrideUserDir
    )
    if ($OverrideUserDir) {
        return [System.IO.Path]::GetFullPath($OverrideUserDir)
    }
    $exe = Get-VcvRackExe -RackExe $RackExe
    if ($exe) {
        $sidecar = Join-Path (Split-Path -Parent $exe) "Rack2"
        if (Test-Path -LiteralPath $sidecar) {
            return (Resolve-Path -LiteralPath $sidecar).Path
        }
    }
    return Join-Path $env:LOCALAPPDATA "Rack2"
}
