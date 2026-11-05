# Builds the release zip for players: dist\MoH2010-PCFix-<version>.zip
#
#   .\tools\package.ps1                 # Release build + tests, then the zip
#   .\tools\package.ps1 -NoBuild        # zip the existing Release build
#
# The zip mirrors the game folder: extract it into "...\steamapps\common\Medal of Honor" and the files
# land in Binaries\ next to moh.exe. ReShade is not included (players install it with the official
# installer, see README).
param(
  [string]$Version = '0.9.0-beta',
  [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$build = Join-Path $root 'build-x86-ninja\Release'
if (-not $NoBuild) {
  Push-Location $root
  try { & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release } finally { Pop-Location }
}

$stage = Join-Path $root "dist\MoH2010-PCFix-$Version"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$bin = Join-Path $stage 'Binaries'
New-Item -ItemType Directory -Force -Path $bin | Out-Null

foreach ($f in 'dinput8.dll', 'MoH2010.PCFix.addon32', 'MoH2010.NvProfile.exe') {
  $src = Join-Path $build $f
  if (-not (Test-Path $src)) { throw "Build output missing: $src" }
  Copy-Item $src $bin
}
Copy-Item (Join-Path $root 'config\MoH2010.PCFix.ini') $bin
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'MoH2010.PCFix.LICENSE.txt')
Copy-Item (Join-Path $root 'docs\INSTALL.txt') (Join-Path $stage 'MoH2010.PCFix.README.txt')

$zip = Join-Path $root "dist\MoH2010-PCFix-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Get-ChildItem $stage -Recurse -File | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName.Substring($stage.Length + 1) }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash
"zip: $zip ($((Get-Item $zip).Length) bytes)"
"sha256: $hash"
