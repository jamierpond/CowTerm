# Copy a built CowTerm.exe and CowTermDaemon.exe into the install directory,
# while the installed ones may still be running.
#
# Windows locks a running exe against overwrite but not against rename, so a
# locked target is renamed aside (<name>.<stamp>.old) and the new one copied in
# its place; the running process carries on from the renamed file, and the
# aside copies go on the next install once nothing runs them.
#
# Nothing running is stopped. The daemon owns every shell, so stopping it
# would end the user's sessions mid-work; instead this says what is still
# running and how to switch over (a new CowTerm attaches to whichever daemon
# is up, so the old one keeps serving until it is told to go).
#
#   Scripts/install-windows.ps1 <build dir> <install dir>
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $InstallDir
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

foreach ($old in Get-ChildItem -Path $InstallDir -Filter '*.old' -ErrorAction SilentlyContinue)
{
    try { Remove-Item -Force $old.FullName } catch { }
}

foreach ($name in 'CowTerm.exe', 'CowTermDaemon.exe')
{
    $source = Join-Path $BuildDir "Terminal\$name"
    $target = Join-Path $InstallDir $name

    if (-not (Test-Path $source))
    {
        Write-Error "$source is missing; build first."
    }

    try
    {
        Copy-Item -Force $source $target
    }
    catch
    {
        $aside = "$name.$(Get-Date -Format yyyyMMddHHmmss).old"
        Rename-Item -Path $target -NewName $aside
        Copy-Item -Force $source $target
        Write-Host "$name was running; the running copy is now $aside"
    }
}

$running = Get-Process -Name CowTerm, CowTermDaemon -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($InstallDir, [StringComparison]::OrdinalIgnoreCase) }

if ($running)
{
    Write-Host ''
    Write-Host 'The CowTerm that was running keeps going, shells and all, on the old build.'
    Write-Host 'To switch: tray icon > Kill everything & quit (ends those shells), then open'
    Write-Host 'CowTerm again. A plain Quit keeps the old daemon, and a new CowTerm would'
    Write-Host 'attach to it.'
}
