# Experiment: MOHAHUDCursor.DefaultCursorData = the 4 crosshair ticks (80-byte structs: PixelStart,
# PixelSize, UV0..UV3, Rotation, RotationOrigin, OffsetVec, bApplyScale at +68). The ticks have
# bApplyScale=0 (other cursors have 1). Sets bApplyScale on every copy, or multiplies the pixel sizes.
# Usage: cursor_scale.ps1 -ApplyScale 1      or   cursor_scale.ps1 -Size 3 (PixelStart/PixelSize x3 from 720p)
param([int]$ApplyScale = -1, [float]$Size = 0)
$hits = & (Join-Path $PSScriptRoot 'memtool.ps1') findf -2 -8 4 8 504 498
$addrs = @(-split $hits | Where-Object { $_ -match '^0x' } | ForEach-Object { [Convert]::ToInt64($_.Substring(2), 16) })
# Tick k: PixelStart, PixelSize from the package defaults (720p pixels).
$ticks = @(@(-2, -8, 4, 8), @(-2, 0, 4, 8), @(-8, -2, 8, 4), @(0, -2, 8, 4))
foreach ($a in $addrs) {
  for ($k = 0; $k -lt 4; $k++) {
    $e = $a + 80 * $k
    if ($ApplyScale -ge 0) { & (Join-Path $PSScriptRoot 'memtool.ps1') writei ('{0:X}' -f ($e + 68)) $ApplyScale | Out-Null }
    if ($Size -gt 0) { for ($j = 0; $j -lt 4; $j++) { & (Join-Path $PSScriptRoot 'memtool.ps1') write ('{0:X}' -f ($e + 4 * $j)) ($ticks[$k][$j] * $Size) | Out-Null } }
  }
}
"$($addrs.Count) cursor table(s): " + ($addrs | ForEach-Object { '0x{0:X8}' -f $_ }) -join ' '
