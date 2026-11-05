# Finds the Medal of Honor (2010) folder in the Steam libraries (dot-source: . .\tools\game-dir.ps1).
function Find-GameDir {
  $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
  $libraries = @()
  if ($steam) {
    $libraries += $steam
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
      foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
        $libraries += $m.Groups[1].Value.Replace('\\', '\')
      }
    }
  }
  foreach ($library in $libraries | Select-Object -Unique) {
    $dir = Join-Path $library 'steamapps\common\Medal of Honor'
    if (Test-Path (Join-Path $dir 'Binaries\moh.exe')) { return $dir }
  }
  throw 'Medal of Honor (2010) not found in the Steam libraries: pass -GameDir "<folder with Binaries>"'
}
