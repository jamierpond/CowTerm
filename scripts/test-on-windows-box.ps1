# The Windows half of scripts/test-on-windows.sh: brings ~/projects/CowTerm-remote
# to origin/<Branch>, lays ~/cowterm-overlay over it, then runs `just test`
# and `just build`.
param([string]$Branch)
$ErrorActionPreference = 'Stop'

$repo = Join-Path $HOME 'projects\CowTerm-remote'
$overlay = Join-Path $HOME 'cowterm-overlay'

if (-not (Test-Path $repo))
{
    git clone https://github.com/jamierpond/CowTerm $repo
}

Set-Location $repo
git fetch -q origin
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
git checkout -q --force --detach "origin/$Branch"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
git clean -q -fd -e build

if (Get-ChildItem $overlay)
{
    Copy-Item -Recurse -Force (Join-Path $overlay '*') $repo
}

just test
$testExitCode = $LASTEXITCODE
just build
$buildExitCode = $LASTEXITCODE
Write-Host "test exit $testExitCode, build exit $buildExitCode"
exit [Math]::Max($testExitCode, $buildExitCode)
