# Highest graphics settings for MoH 2010 single player (game closed).
#
# The game's hardware detection fails on current GPUs (GPU timing pass gives 0 ms, VRAM read as 3 GB)
# and files an RTX 4080 SUPER under AppCompatBucket1 = "Minspec": 800x600, no dynamic shadows, no depth
# of field, anisotropic filtering 1x, decals culled at distance 0. This script writes the game's own
# "High end" preset (BaseEngine.ini defaults + DefaultCompat.ini [AppCompatBucket3]) into
# Documents\EA Games\Medal of Honor\Config\MOHAEngine.ini, plus:
#   - MaxAnisotropy=16, MaxShadowResolution=2048 (sharper dynamic shadows; beyond the preset),
#   - Distortion on (heat haze / refraction; off in the base config),
#   - CompatLevelGPU/Composite=3, so the menu's "Restore defaults" picks the high-end bucket, not minspec.
# Anti-aliasing stays MaxMultisamples=1: the game's own AA does nothing in single player (DX9); use
# driver SGSSAA (see README).
#
#   .\tools\max-graphics.ps1            # apply (backup next to the ini: MOHAEngine.ini.bak-<date>)
#   .\tools\max-graphics.ps1 -WhatIf    # only show what would change
param(
  [string]$ConfigDir = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'EA Games\Medal of Honor\Config'),
  [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
if (-not $WhatIf -and (Get-Process moh -ErrorAction SilentlyContinue)) { throw 'Medal of Honor is running. Close it first (it keeps its settings in memory).' }
$path = Join-Path $ConfigDir 'MOHAEngine.ini'
if (-not (Test-Path $path)) { throw "$path not found (start the game once first)" }

# key -> value; $true/$false are written in the file's own spelling (localized on a Russian install).
$settings = [ordered]@{
  'SystemSettings' = [ordered]@{
    DynamicShadows = $true; LightEnvironmentShadows = $true; DepthOfField = $true; UseHighQualityBloom = $true
    Distortion = $true; DropParticleDistortion = $false; AmbientOcclusion = $true; Bloom = $true; MotionBlur = $true
    LensFlares = $true; FogVolumes = $true; DynamicLights = $true; StaticDecals = $true; DynamicDecals = $true
    SpeedTreeLeaves = $true; SpeedTreeFronds = $true; DirectionalLightmaps = $true; bAllowFracturedDamage = $true
    DecalCullDistanceScale = '1.000000'; DetailMode = '2'; SkeletalMeshLODBias = '0'; ParticleLODBias = '0'
    MaxAnisotropy = '16'; MaxShadowResolution = '2048'; CSMDistanceScale = '2.000000'; CSMSizeScale = '2.000000'
    DOWNVFX = $false; MinSpecCull = $false; MaxMultisamples = '1'; ScreenPercentage = '100.000000'
  }
  'AppCompat' = [ordered]@{ CompatLevelComposite = '3'; CompatLevelGPU = '3' }
}

$bytes = [IO.File]::ReadAllBytes($path)
$encoding = if ($bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) { [Text.Encoding]::Unicode } else { [Text.Encoding]::Default }
$text = [IO.File]::ReadAllText($path, $encoding)
$changes = 0

# Cyrillic as char codes: Windows PowerShell 5.1 reads this file as ANSI.
$Da = [string][char]0x0414 + [char]0x0430                   # Да
$Net = [string][char]0x041D + [char]0x0435 + [char]0x0442    # Нет

function Format-Bool([bool]$value, [string]$old) {
  switch -Regex ($old) {
    "^($Da|$Net)$" { if ($value) { return $Da } else { return $Net } }
    '^(TRUE|FALSE)$' { if ($value) { return 'TRUE' } else { return 'FALSE' } }
    default { if ($value) { return 'True' } else { return 'False' } }
  }
}

foreach ($section in $settings.Keys) {
  $header = [regex]::Match($text, "(?m)^\[$([regex]::Escape($section))\]\r?$")
  if (-not $header.Success) { Write-Warning "[$section] not found"; continue }
  $next = [regex]::Match($text.Substring($header.Index + $header.Length), "(?m)^\[")
  $end = if ($next.Success) { $header.Index + $header.Length + $next.Index } else { $text.Length }
  foreach ($key in $settings[$section].Keys) {
    $body = $text.Substring($header.Index, $end - $header.Index)
    $m = [regex]::Match($body, "(?m)^$([regex]::Escape($key))=([^\r\n]*)")
    $wanted = $settings[$section][$key]
    if ($m.Success) {
      $old = $m.Groups[1].Value
      $new = if ($wanted -is [bool]) { Format-Bool $wanted $old } else { [string]$wanted }
      if ($old -eq $new) { continue }
      $at = $header.Index + $m.Groups[1].Index
      $text = $text.Substring(0, $at) + $new + $text.Substring($at + $old.Length)
      $end += $new.Length - $old.Length
      Write-Host "  [$section] $key=$old -> $new"
    } else {
      $sample = [regex]::Match($body, "(?m)=($Da|$Net)\r?`$")
      $new = if ($wanted -is [bool]) { Format-Bool $wanted ($(if ($sample.Success) { $sample.Groups[1].Value } else { '' })) } else { [string]$wanted }
      $line = "$key=$new`r`n"
      $text = $text.Substring(0, $end) + $line + $text.Substring($end)
      $end += $line.Length
      Write-Host "  [$section] $key=$new (added)"
    }
    $changes++
  }
}

if ($changes -eq 0) { Write-Host 'Already at the highest settings.'; return }
if ($WhatIf) { Write-Host "$changes change(s) (not written: -WhatIf)"; return }
$backup = "$path.bak-$(Get-Date -Format yyyyMMdd-HHmmss)"
Copy-Item $path $backup
[IO.File]::WriteAllText($path, $text, $encoding)
Write-Host "$changes change(s) written. Backup: $backup"
