# Time from Pty::start to a usable PowerShell prompt, several runs.
#
#   Scripts/measure-shell-start.ps1 [build-dir] [runs]
#
# Builds CowTermShellStartTimer in the build tree and runs it; each run prints
# milliseconds. Run it twice after a Visual Studio update: the first run pays
# for the developer-environment capture, later runs read the cache.
param(
    [string] $BuildDir = 'build',
    [int] $Runs = 5
)
$ErrorActionPreference = 'Stop'

# In a child PowerShell, so the developer environment with-msvc.ps1 sets up
# stays out of this one and out of the shells being timed.
powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/with-msvc.ps1" `
    cmake --build $BuildDir --target CowTermShellStartTimer
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$timer = Join-Path $BuildDir 'Test/CowTermShellStartTimer.exe'
$times = foreach ($run in 1..$Runs) { [int] (& $timer) }
Write-Host "runs (ms): $($times -join ', ')"
Write-Host "median (ms): $(($times | Sort-Object)[[math]::Floor($Runs / 2)])"
