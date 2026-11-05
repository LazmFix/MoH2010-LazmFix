# Saves a screenshot of the game (only while the game is the foreground window), downscaled.
# Usage: shot.ps1 <out.jpg> [-Width 1920] [-Region x,y,w,h (in screen pixels)] [-Process moh]
param([Parameter(Mandatory)][string]$Out, [int]$Width = 1920, [int[]]$Region, [string]$Process = 'moh')
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Fg {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[Fg]::SetProcessDPIAware() | Out-Null
$game = Get-Process $Process -ErrorAction Stop | Select-Object -First 1
$fgPid = 0; [Fg]::GetWindowThreadProcessId([Fg]::GetForegroundWindow(), [ref]$fgPid) | Out-Null
if ($fgPid -ne $game.Id) { Write-Error "$Process is not the foreground window"; exit 1 }
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
if ($Region) { $rect = New-Object Drawing.Rectangle $Region[0], $Region[1], $Region[2], $Region[3] } else { $rect = $screen }
$bmp = New-Object Drawing.Bitmap $rect.Width, $rect.Height
$g = [Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($rect.X, $rect.Y, 0, 0, $bmp.Size); $g.Dispose()
$w = [Math]::Min($Width, $rect.Width); $h = [int]($rect.Height * $w / $rect.Width)
$small = New-Object Drawing.Bitmap $bmp, $w, $h
$small.Save($Out, [Drawing.Imaging.ImageFormat]::Jpeg)
"saved $Out ($w x $h from $($rect.Width) x $($rect.Height))"
