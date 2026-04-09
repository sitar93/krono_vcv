param(
    [string]$RackDir = "D:\Files\VCV\Rack-SDK",
    [string]$MsysRoot = "C:\msys64",
    [switch]$InstallToRack
)

$ErrorActionPreference = "Stop"

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

Write-Host "[krono] Running build in MSYS2 (MINGW64)..."
& $bashExe -lc "set -euo pipefail; export MSYSTEM=MINGW64; export CHERE_INVOKING=1; export RACK_DIR='$msysRackDir'; bash '$msysBuildScript'"
if ($LASTEXITCODE -ne 0) {
    throw "MSYS2 build failed with exit code $LASTEXITCODE"
}

if ($InstallToRack) {
    Write-Host "[krono] Installing plugin into Rack user folder..."
    $slug = (Get-Content (Join-Path $pluginDir "plugin.json") | ConvertFrom-Json).slug
    $pluginsDirMsys = Convert-ToMsysPath -WindowsPath (Join-Path $env:LOCALAPPDATA "Rack2\plugins-win-x64")
    & $bashExe -lc "set -euo pipefail; export MSYSTEM=MINGW64; export CHERE_INVOKING=1; export PATH='/mingw64/bin:/usr/bin:$PATH'; export CC='gcc'; export CXX='g++'; export RACK_DIR='$msysRackDir'; make -C '$msysPluginDir' dist; mkdir -p '$pluginsDirMsys'; rm -rf '$pluginsDirMsys/$slug'; zstd -dc '$msysPluginDir/dist/'*.vcvplugin | tar -x -C '$pluginsDirMsys'"
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 install failed with exit code $LASTEXITCODE"
    }
}

Write-Host "[krono] Done."
