# Run a build command with the MSVC toolchain in scope.
#
# The Ninja generator drives cl and link directly, so it needs the environment
# a "Developer PowerShell for VS" sets up (PATH, INCLUDE, LIB); a plain shell
# has none of it, and CMake then fails to find a C++ compiler at all. When cl
# is already on PATH -- a developer shell, or CI that set one up -- the command
# runs unchanged. Otherwise vswhere finds the newest Visual Studio with the C++
# tools and its Launch-VsDevShell.ps1 brings that environment into this
# process, for the host's own architecture (arm64 on an ARM64 machine, so the
# build is native rather than emulated x64).
#
#   Scripts/with-msvc.ps1 cmake --build build
$ErrorActionPreference = 'Stop'

if ($args.Count -eq 0)
{
    Write-Error 'usage: Scripts/with-msvc.ps1 <command> [args...]'
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (-not (Test-Path $vswhere))
    {
        Write-Error "vswhere not found at $vswhere. Install Visual Studio (Community is enough) with the 'Desktop development with C++' workload."
    }

    # -prerelease too: a Preview install is as good a toolchain as any.
    $vs = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64, Microsoft.VisualStudio.Component.VC.Tools.ARM64 `
        -requiresAny -property installationPath

    if (-not $vs)
    {
        Write-Error "No Visual Studio with the C++ build tools found. Add the 'Desktop development with C++' workload in the Visual Studio Installer."
    }

    $devShell = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'

    if (-not (Test-Path $devShell))
    {
        Write-Error "$devShell is missing; repair the Visual Studio install."
    }

    # VsDevCmd.bat calls vswhere by bare name and complains when it isn't on
    # PATH, though it finds its way regardless.
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"

    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'amd64' }

    # The script prints a banner and, by default, changes directory to the
    # user's source folder; neither is wanted mid-recipe.
    & $devShell -Arch $arch -HostArch $arch -SkipAutomaticLocation | Out-Null

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue))
    {
        Write-Error "Launch-VsDevShell.ps1 ran but cl.exe is still not on PATH ($vs)."
    }
}

$command = $args[0]
$rest = @($args | Select-Object -Skip 1)
& $command @rest
exit $LASTEXITCODE
