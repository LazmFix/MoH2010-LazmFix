# Waits for moh.exe (started from Steam), then reports whether it got past the splash screen and
# prints the fix's log. Usage: tools\wait-and-check.ps1 [-WaitMinutes 30] [-Seconds 90]
param([int]$WaitMinutes = 30, [int]$Seconds = 90)

. (Join-Path $PSScriptRoot 'game-dir.ps1')
$bin = Join-Path (Find-GameDir) 'Binaries'
$deadline = (Get-Date).AddMinutes($WaitMinutes)
while (-not (Get-Process moh -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (-not (Get-Process moh -ErrorAction SilentlyContinue)) { "moh.exe did not start within $WaitMinutes min"; exit 1 }
$start = Get-Date
"moh.exe started $($start.ToString('HH:mm:ss'))"
$reached = $false
for ($i = 0; $i -lt $Seconds; $i++) {
  Start-Sleep 1
  $p = Get-Process moh -ErrorAction SilentlyContinue
  if (-not $p) { "moh.exe exited after $i s"; break }
  if (-not $reached -and $p.MainWindowTitle -eq 'Medal of Honor') {
    $reached = $true
    "past the splash screen after $i s (window 'Medal of Honor', $($p.Threads.Count) threads, $([int]($p.WorkingSet64/1MB)) MB)"
  }
}
$p = Get-Process moh -ErrorAction SilentlyContinue
if ($p) { "now: title='$($p.MainWindowTitle)' threads=$($p.Threads.Count) ws=$([int]($p.WorkingSet64/1MB))MB cpu=$([int]$p.CPU)s" }
'--- MoH2010.PCFix.log'
Get-Content (Join-Path $bin 'MoH2010.PCFix.log') -ErrorAction SilentlyContinue
'--- ReShade.log (add-on lines)'
Select-String -Path (Join-Path $bin 'ReShade.log') -Pattern 'add-on|addon|MoH2010|ERROR|Initializ' -ErrorAction SilentlyContinue | ForEach-Object { $_.Line }
