# Installs the local build into the game folder (developer tool; players extract the release zip).
# Game closed. ReShade is not installed by this script: use the official installer (README).
#
#   .\tools\install.ps1                       # finds the game in the Steam libraries
#   .\tools\install.ps1 -GameDir "D:\Games\Medal of Honor"
#
# Replaced files are saved to Binaries\MoH2010.PCFix.backup\<date>\ with install.json;
# tools\uninstall.ps1 puts them back. An existing MoH2010.PCFix.ini is kept.
param(
  [string]$GameDir = '',
  [string]$BuildDir = (Join-Path $PSScriptRoot '..\build-x86-ninja\Release')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'game-dir.ps1')
if (-not $GameDir) { $GameDir = Find-GameDir }
$bin = Join-Path $GameDir 'Binaries'
$exe = Join-Path $bin 'moh.exe'
if (-not (Test-Path $exe)) { throw "moh.exe not found: $exe" }
if (Get-Process moh -ErrorAction SilentlyContinue) { throw 'Medal of Honor is running. Close it first.' }

$core = Join-Path $BuildDir 'dinput8.dll'
$addon = Join-Path $BuildDir 'MoH2010.PCFix.addon32'
$nvProfile = Join-Path $BuildDir 'MoH2010.NvProfile.exe'
foreach ($f in $core, $addon, $nvProfile) { if (-not (Test-Path $f)) { throw "Build output missing: $f (run tools\build.ps1 -Configuration Release)" } }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $bin "MoH2010.PCFix.backup\$stamp"
New-Item -ItemType Directory -Force -Path $backup | Out-Null
$manifest = [ordered]@{ date = $stamp; game_exe_sha256 = (Get-FileHash $exe -Algorithm SHA256).Hash; created = @(); replaced = @(); updated = @(); ini = @() }

function Place([string]$source, [string]$name) {
  $target = Join-Path $bin $name
  if ((Test-Path $target) -and (Get-Item $target).VersionInfo.ProductName -eq 'MoH 2010 PC Fix') {
    # Our own file from an earlier install: an update, the original state is in that earlier backup.
    $manifest.updated += $name
  } elseif (Test-Path $target) {
    Copy-Item $target (Join-Path $backup $name) -Force
    $manifest.replaced += $name
  } else {
    $manifest.created += $name
  }
  Copy-Item $source $target -Force
  Write-Host "  $name"
}

Write-Host "Game folder: $bin"
$existing = Join-Path $bin 'dinput8.dll'
if ((Test-Path $existing) -and ((Get-Item $existing).VersionInfo.ProductName -ne 'MoH 2010 PC Fix')) {
  Write-Warning "A different dinput8.dll is in the game folder; it is backed up and replaced."
}
Place $core 'dinput8.dll'
Place $addon 'MoH2010.PCFix.addon32'
Place $nvProfile 'MoH2010.NvProfile.exe'
if (-not (Test-Path (Join-Path $bin 'MoH2010.PCFix.ini'))) {
  Place (Join-Path $PSScriptRoot '..\config\MoH2010.PCFix.ini') 'MoH2010.PCFix.ini'
} else {
  Write-Host '  MoH2010.PCFix.ini (kept: your settings)'
}
$d3d9 = Join-Path $bin 'd3d9.dll'
if ((Test-Path $d3d9) -and (Get-Item $d3d9).VersionInfo.ProductName -eq 'ReShade') {
  Write-Host "  ReShade $((Get-Item $d3d9).VersionInfo.ProductVersion) found"
} else {
  Write-Warning 'ReShade (d3d9.dll) not found: install "ReShade with full add-on support" for moh.exe (Direct3D 9) for the settings panel and the borderless window.'
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $backup 'install.json') -Encoding UTF8
Write-Host "Backup and manifest: $backup"
Write-Host 'Done. Start the game from Steam; log: Binaries\MoH2010.PCFix.log; ReShade menu: Home -> Add-ons -> MoH 2010 PC Fix.'
