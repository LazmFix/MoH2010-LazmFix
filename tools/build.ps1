param(
  [ValidateSet('Debug', 'Release', 'All')]
  [string]$Configuration = 'All',
  [string]$BuildDir = 'build-x86-ninja'
)

$ErrorActionPreference = 'Stop'

$canonicalPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
if (-not $canonicalPath) {
  $canonicalPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
}
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $canonicalPath, 'Process')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
  throw 'vswhere.exe was not found. Install Visual Studio 2022 Build Tools.'
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
  throw 'Visual Studio 2022 Build Tools with MSVC (x86/x64) tools was not found.'
}

$vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) {
  throw "VsDevCmd.bat was not found at $vsDevCmd"
}

$cmakePath = $null
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) {
  $cmakePath = $cmake.Source
}

if (-not $cmakePath) {
  $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  if (Test-Path $candidate) {
    $cmakePath = $candidate
  }
}

if (-not $cmakePath) {
  throw 'cmake was not found in PATH. Install Visual Studio 2022 Build Tools with C++ and CMake, then run from Developer PowerShell.'
}

$ninjaPath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $ninjaPath)) {
  $ninja = Get-Command ninja -ErrorAction SilentlyContinue
  if (-not $ninja) {
    throw 'ninja was not found in Visual Studio Build Tools or PATH.'
  }
  $ninjaPath = $ninja.Source
}

$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path $ctestPath)) {
  $ctest = Get-Command ctest -ErrorAction SilentlyContinue
  if (-not $ctest) {
    throw 'ctest was not found next to cmake or in PATH.'
  }
  $ctestPath = $ctest.Source
}

$configs = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
$buildTemp = Join-Path (Get-Location) "$BuildDir\.tmp"
New-Item -ItemType Directory -Force -Path $buildTemp | Out-Null

function Invoke-X86DevCommand {
  param(
    [Parameter(Mandatory = $true)][string]$Command,
    [switch]$AllowFailure
  )
  $cmd = "set `"TEMP=$buildTemp`" && set `"TMP=$buildTemp`" && `"$vsDevCmd`" -arch=x86 -host_arch=x64 >nul && $Command"
  & cmd.exe /s /c $cmd
  if ($LASTEXITCODE -ne 0 -and -not $AllowFailure) {
    throw "Command failed with exit code ${LASTEXITCODE}: $Command"
  }
}

Invoke-X86DevCommand "where cl"
Invoke-X86DevCommand "`"$cmakePath`" -S . -B `"$BuildDir`" -G `"Ninja Multi-Config`" -D CMAKE_MAKE_PROGRAM=`"$ninjaPath`" -D CMAKE_CONFIGURATION_TYPES=`"Debug;Release`""

foreach ($config in $configs) {
  Invoke-X86DevCommand "`"$cmakePath`" --build `"$BuildDir`" --config $config"
  Invoke-X86DevCommand "`"$ctestPath`" --test-dir `"$BuildDir`" -C $config --output-on-failure"
}
