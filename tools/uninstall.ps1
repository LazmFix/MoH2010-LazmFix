# Removes MoH 2010 LazmFix (game closed): takes the newest backup in Binaries\MoH2010.LazmFix.backup\,
# deletes the files the install created, puts back the ones it replaced and reverts the ini keys it
# changed (only where they still have the value the install wrote).
#
#   .\tools\uninstall.ps1                # everything (fix, ReShade it added, ini tweaks)
#   .\tools\uninstall.ps1 -KeepReShade   # leave d3d9.dll / ReShade.ini
param(
  [string]$GameDir = '',
  [string]$ConfigDir = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'EA Games\Medal of Honor\Config'),
  [switch]$KeepReShade
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'game-dir.ps1')
if (-not $GameDir) { $GameDir = Find-GameDir }
$bin = Join-Path $GameDir 'Binaries'
if (Get-Process moh -ErrorAction SilentlyContinue) { throw 'Medal of Honor is running. Close it first.' }

# Driver anti-aliasing set from the ReShade panel: put the NVIDIA game profile back first (UAC prompt).
$nvTool = Join-Path $bin 'MoH2010.LazmFix.NvProfile.exe'
if ((Test-Path $nvTool) -and (Test-Path (Join-Path $bin 'MoH2010.LazmFix.NvProfile.backup.txt'))) {
  Write-Host 'Restoring the NVIDIA profile "Medal of Honor" (administrator prompt)...'
  try { Start-Process $nvTool -ArgumentList 'restore' -WorkingDirectory $bin -Verb RunAs -Wait -WindowStyle Hidden }
  catch { Write-Warning "NVIDIA profile not restored: $($_.Exception.Message). Run '$nvTool restore' as administrator." }
}
foreach ($f in 'MoH2010.LazmFix.NvProfile.log') { Remove-Item (Join-Path $bin $f) -ErrorAction SilentlyContinue }
$root = Join-Path $bin 'MoH2010.LazmFix.backup'
$backups = @(Get-ChildItem $root -Directory -ErrorAction SilentlyContinue | Sort-Object Name |
  Where-Object { Test-Path (Join-Path $_.FullName 'install.json') })
if ($backups.Count -eq 0) { throw "No backup found in $root" }

# Several installs (updates) leave several backups. The oldest one that mentions a file or ini key
# holds its state from before the fix.
$fileState = [ordered]@{}   # name -> @{ action = 'created' | 'replaced'; dir }
$iniState = [ordered]@{}    # "file|section|key" -> change (old = original value)
$lastNew = @{}              # same key -> value the newest install wrote
foreach ($b in $backups) {
  $m = Get-Content (Join-Path $b.FullName 'install.json') -Raw | ConvertFrom-Json
  foreach ($name in @($m.created)) { if ($name -and -not $fileState.Contains($name)) { $fileState[$name] = @{ action = 'created'; dir = $b.FullName } } }
  foreach ($name in @($m.replaced)) { if ($name -and -not $fileState.Contains($name)) { $fileState[$name] = @{ action = 'replaced'; dir = $b.FullName } } }
  foreach ($c in @($m.ini)) {
    if (-not $c) { continue }
    $k = "$($c.file)|$($c.section)|$($c.key)"
    if (-not $iniState.Contains($k)) { $iniState[$k] = $c }
    $lastNew[$k] = $c.new
  }
}
Write-Host "Backups: $($backups.Count) (oldest $($backups[0].Name))"

$reshadeFiles = @('d3d9.dll', 'ReShade.ini')
foreach ($name in $fileState.Keys) {
  if ($KeepReShade -and $reshadeFiles -contains $name) { continue }
  $state = $fileState[$name]
  $path = Join-Path $bin $name
  if ($state.action -eq 'created') {
    if (Test-Path $path) { Remove-Item $path -Force; Write-Host "  removed $name" }
  } else {
    Copy-Item (Join-Path $state.dir $name) $path -Force
    Write-Host "  restored $name"
  }
}
$manifest = @{ ini = @($iniState.Values | ForEach-Object { $_.new = $lastNew["$($_.file)|$($_.section)|$($_.key)"]; $_ }) }
Get-ChildItem $bin -Filter 'MoH2010.LazmFix.log' -ErrorAction SilentlyContinue | Remove-Item -Force

# Intro videos hidden by the panel's "Skip intro videos".
Get-ChildItem (Join-Path $GameDir 'MOHAGame\CookedMovies') -Filter '*.vp6.skip' -ErrorAction SilentlyContinue | ForEach-Object {
  $original = $_.FullName.Substring(0, $_.FullName.Length - '.skip'.Length)
  if (-not (Test-Path $original)) { Rename-Item $_.FullName (Split-Path $original -Leaf); Write-Host "  restored $(Split-Path $original -Leaf)" }
}

function Read-Ini([string]$path) {
  $bytes = [IO.File]::ReadAllBytes($path)
  if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) { $enc = [Text.Encoding]::Unicode }
  elseif ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) { $enc = New-Object Text.UTF8Encoding($true) }
  else { $enc = [Text.Encoding]::Default }
  return @{ Text = [IO.File]::ReadAllText($path, $enc); Encoding = $enc }
}

foreach ($change in @($manifest.ini)) {
  $path = Join-Path $ConfigDir $change.file
  if (-not (Test-Path $path)) { continue }
  $ini = Read-Ini $path
  $pattern = "(?ms)(^\[$([regex]::Escape($change.section))\]\r?\n.*?^$([regex]::Escape($change.key))=)([^\r\n]*)"
  $m = [regex]::Match($ini.Text, $pattern)
  if (-not $m.Success) { continue }
  if ($m.Groups[2].Value -ne $change.new) {
    Write-Host "  $($change.file) $($change.key)=$($m.Groups[2].Value) (changed since install, kept)"
    continue
  }
  $text = $ini.Text.Substring(0, $m.Groups[2].Index) + $change.old + $ini.Text.Substring($m.Groups[2].Index + $m.Groups[2].Length)
  [IO.File]::WriteAllText($path, $text, $ini.Encoding)
  Write-Host "  $($change.file) $($change.key)=$($change.new) -> $($change.old)"
}
Write-Host 'Done. The backup folder is kept; delete it by hand if you like.'
