# Copies dist/*.vcvplugin into release/ and writes a .sha256 sidecar per file (hex only, uppercase), e.g. Krono-2.1.0-win-x64.sha256.
# Optionally patches release/release_notes.txt SHA256 line for the first win-x64 package.
# Usage: .\scripts\prepare_assets_from_dist.ps1 [-UpdateReleaseNotes]
# Prerequisite: make dist (e.g. via install_to_rack_msys2.sh) so dist/ contains Krono-*.vcvplugin

param(
    [string]$DistPath = "",
    [string]$ReleaseDir = "",
    [switch]$UpdateReleaseNotes
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if (-not $DistPath) { $DistPath = Join-Path $root "dist" }
if (-not $ReleaseDir) { $ReleaseDir = Join-Path $root "release" }

if (-not (Test-Path -LiteralPath $DistPath)) {
    throw "dist folder missing: $DistPath - run make dist first."
}

New-Item -ItemType Directory -Force -Path $ReleaseDir | Out-Null
$plugins = @(Get-ChildItem -LiteralPath $DistPath -Filter "Krono-*.vcvplugin" -File)
if ($plugins.Count -eq 0) {
    throw "No Krono-*.vcvplugin in $DistPath"
}

$firstWinHash = $null
foreach ($p in $plugins) {
    $dest = Join-Path $ReleaseDir $p.Name
    Copy-Item -LiteralPath $p.FullName -Destination $dest -Force
    $hex = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($p.Name)
    $shaPath = Join-Path $ReleaseDir ($stem + ".sha256")
    Set-Content -LiteralPath $shaPath -Value $hex -Encoding ascii -NoNewline
    Write-Host "[krono] OK $($p.Name) -> $ReleaseDir"
    Write-Host "[krono]     SHA256 $hex -> $(Split-Path $shaPath -Leaf)"
    if ($p.Name -match "win-x64" -and -not $firstWinHash) { $firstWinHash = $hex }
}

if ($UpdateReleaseNotes -and $firstWinHash) {
    $notes = Join-Path $ReleaseDir "release_notes.txt"
    if (Test-Path -LiteralPath $notes) {
        $c = Get-Content -LiteralPath $notes -Raw
        $c2 = $c.Replace('`REPLACE_WITH_HEX_AFTER_PREPARE_SCRIPT`', ('`' + $firstWinHash + '`'))
        $utf8 = New-Object System.Text.UTF8Encoding $false
        [System.IO.File]::WriteAllText($notes, $c2.TrimEnd("`r", "`n") + "`n", $utf8)
        Write-Host "[krono] Updated SHA256 in release_notes.txt"
    }
}

Write-Host ""
Write-Host "[krono] GitHub Releases: upload .vcvplugin + .sha256 from $ReleaseDir"
Write-Host "[krono] Paste release_notes.txt into the release description if needed."
