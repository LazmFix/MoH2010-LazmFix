# Starts moh.exe directly, without Steam's CD key dialog. Steam must be running: with SteamAppId set
# the game's Steam check passes (without it: "Application load error 5:0000065434").
# Note: Steam does not count play time for launches made this way.
param(
  [string]$GameDir = '',
  [switch]$Foreground   # bring the window to the front once it exists (when started from a background shell)
)

. (Join-Path $PSScriptRoot 'game-dir.ps1')
if (-not $GameDir) { $GameDir = Find-GameDir }
$bin = Join-Path $GameDir 'Binaries'
if (Get-Process moh -ErrorAction SilentlyContinue) { Write-Host 'Medal of Honor is already running.'; return }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { Write-Warning 'Steam is not running; start it first.' }

$psi = New-Object Diagnostics.ProcessStartInfo (Join-Path $bin 'moh.exe')
$psi.WorkingDirectory = $bin
$psi.UseShellExecute = $false
$psi.EnvironmentVariables['SteamAppId'] = '47790'
$psi.EnvironmentVariables['SteamGameId'] = '47790'
$process = [Diagnostics.Process]::Start($psi)
Write-Host "moh.exe started (pid $($process.Id))"

if ($Foreground) {
  Add-Type @'
using System; using System.Runtime.InteropServices;
public static class MohFg {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
}
'@
  for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Milliseconds 500
    $p = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
    if (-not $p) { break }
    if ($p.MainWindowTitle -eq 'Medal of Honor') {
      [MohFg]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)   # Alt: lets this process hand over the foreground
      [MohFg]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
      [MohFg]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
      [MohFg]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
      Write-Host "game window in front after $([int]($i / 2)) s"
      break
    }
  }
}
