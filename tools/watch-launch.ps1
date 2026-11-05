# Watches moh.exe after a launch: start/exit, threads, memory, window title, exit code.
# Usage: powershell -File tools\watch-launch.ps1 [-Seconds 90] [-Launch]
param([int]$Seconds = 90, [switch]$Launch)

$log = Join-Path $PSScriptRoot "..\logs\launch-$(Get-Date -Format yyyyMMdd_HHmmss).txt"
function W($m) { $l = "{0:HH:mm:ss.fff} {1}" -f (Get-Date), $m; $l; Add-Content -Path $log -Value $l }

$t0 = Get-Date
if ($Launch) { W "launch steam://rungameid/47790"; Start-Process "steam://rungameid/47790" }

$seen = @{}
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
    foreach ($p in Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^(moh|pbsvc|DXSETUP|PhysX|vcredist)' }) {
        $key = $p.Id
        $info = "{0} pid={1} thr={2} ws={3}MB title='{4}' resp={5}" -f $p.Name, $p.Id, $p.Threads.Count, [int]($p.WorkingSet64/1MB), $p.MainWindowTitle, $p.Responding
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $p
            try { $p.EnableRaisingEvents = $true } catch {}
            W "START $info"
        } elseif ($seen[$key].LastInfo -ne $info) { W "  $info" }
        $seen[$key] | Add-Member -NotePropertyName LastInfo -NotePropertyValue $info -Force
    }
    foreach ($k in @($seen.Keys)) {
        $p = $seen[$k]
        if ($p.HasExited -and -not $p.Reported) {
            $code = try { '0x{0:X8}' -f $p.ExitCode } catch { '?' }
            W "EXIT $($p.Name) pid=$k code=$code"
            $p | Add-Member -NotePropertyName Reported -NotePropertyValue $true -Force
        }
    }
    Start-Sleep -Milliseconds 500
}

W "--- Application log errors since launch"
Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$t0} -ErrorAction SilentlyContinue |
    Where-Object { $_.LevelDisplayName -match 'Error|Ошибка' -or $_.Id -in 1000,1001,1002,1005 } |
    ForEach-Object { W ("[{0}] {1}: {2}" -f $_.Id, $_.ProviderName, ($_.Message -replace '\s+', ' ')) }
W "log: $log"
