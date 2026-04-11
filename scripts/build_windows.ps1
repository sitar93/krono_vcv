param(
    [string]$RackDir = "D:\Files\VCV\Rack-SDK",
    [string]$MsysRoot = "C:\msys64",
    [string]$RackUserDir = "",
    [string]$RackExe = "",
    [switch]$InstallToRack
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "vcv_rack_paths.ps1")

if (!$RackUserDir -and $env:RACK_USER_DIR) {
    $RackUserDir = $env:RACK_USER_DIR
}

function Convert-ToMsysPath {
    param([string]$WindowsPath)

    $full = [System.IO.Path]::GetFullPath($WindowsPath)
    if ($full -match '^([A-Za-z]):\\(.*)$') {
        $drive = $Matches[1].ToLower()
        $rest = $Matches[2] -replace '\\', '/'
        return "/$drive/$rest"
    }

    throw "Cannot convert path to MSYS2 format: $WindowsPath"
}

$bashExe = Join-Path $MsysRoot "usr\bin\bash.exe"
if (!(Test-Path $bashExe)) {
    throw "MSYS2 bash not found at '$bashExe'. Install MSYS2 or pass -MsysRoot."
}

if (!(Test-Path $RackDir)) {
    throw "Rack SDK not found at '$RackDir'. Pass the correct path with -RackDir."
}

$pluginDir = Split-Path -Parent $PSScriptRoot
$msysPluginDir = Convert-ToMsysPath -WindowsPath $pluginDir
$msysRackDir = Convert-ToMsysPath -WindowsPath $RackDir
$msysBuildScript = Convert-ToMsysPath -WindowsPath (Join-Path $PSScriptRoot "build_msys2.sh")
$msysInstallScript = Convert-ToMsysPath -WindowsPath (Join-Path $PSScriptRoot "install_to_rack_msys2.sh")

Write-Host "[krono] Running build in MSYS2 (MINGW64)..."
& $bashExe -lc "set -euo pipefail; export MSYSTEM=MINGW64; export CHERE_INVOKING=1; export RACK_DIR='$msysRackDir'; bash '$msysBuildScript'"
if ($LASTEXITCODE -ne 0) {
    throw "MSYS2 build failed with exit code $LASTEXITCODE"
}

if ($InstallToRack) {
    if (!$RackExe) {
        $RackExe = Get-VcvRackExe
    }
    $userDirWin = Get-VcvRackUserDir -RackExe $RackExe -OverrideUserDir $RackUserDir
    $pluginsWin = Join-Path $userDirWin "plugins-win-x64"
    Write-Host "[krono] Rack user folder: $userDirWin"
    Write-Host "[krono] plugins-win-x64:    $pluginsWin"

    $slug = (Get-Content (Join-Path $pluginDir "plugin.json") | ConvertFrom-Json).slug
    $pluginsDirMsys = Convert-ToMsysPath -WindowsPath $pluginsWin

    & $bashExe -lc "set -euo pipefail; export MSYSTEM=MINGW64; export CHERE_INVOKING=1; bash '$msysInstallScript' '$msysPluginDir' '$pluginsDirMsys' '$msysRackDir' '$slug'"
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 install failed with exit code $LASTEXITCODE"
    }

    $pkg = Get-ChildItem -LiteralPath $pluginsWin -Filter "$slug*.vcvplugin" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime | Select-Object -Last 1
    if (!$pkg) {
        throw "Install reported success but no ${slug}*.vcvplugin in: $pluginsWin"
    }
    Write-Host "[krono] Verified package: $($pkg.FullName)"
}

Write-Host "[krono] Done."
