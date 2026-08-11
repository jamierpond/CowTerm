# CowTerm — cross-platform build / run / install.
# Requires: just, cmake, ninja (+ a C++20 toolchain). See README.md.
#
#   just build            configure + build the app (and its bundled daemon)
#   just run              build, then launch the app
#   just install          build, then install it to the usual place for this OS
#   just build_type=Debug build      override the build type
#   just clean            delete the build tree

# On Windows, run recipe bodies through PowerShell (the OS-specific recipes
# below use PowerShell syntax); macOS/Linux use the default sh.
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

# --- Config (override on the command line, e.g. `just build_type=Debug build`) ---
build_dir  := "build"
generator  := "Ninja"
build_type := "Release"
signing_identity := ""
allow_adhoc_signing := "OFF"

# Default: show the recipe list.
default:
    @just --list

# Configure the CMake build tree (idempotent; first run fetches deps via CPM).
configure:
    cmake -S . -B {{build_dir}} -G "{{generator}}" -DCMAKE_BUILD_TYPE={{build_type}} -DCOWTERM_MACOS_SIGNING_IDENTITY="{{signing_identity}}" -DCOWTERM_MACOS_ALLOW_ADHOC_SIGNING={{allow_adhoc_signing}}

# Build the app; CowTermDaemon builds as a dependency and is bundled alongside it.
build: configure
    cmake --build {{build_dir}} --target CowTermApp

# Build and run the unit tests (regression coverage, incl. the quit-hang fix).
test: configure
    cmake --build {{build_dir}} --target CowTermTests
    ctest --test-dir {{build_dir}} --output-on-failure

# Remove the build tree.
[unix]
clean:
    rm -rf {{build_dir}}

[windows]
clean:
    if (Test-Path "{{build_dir}}") { Remove-Item -Recurse -Force "{{build_dir}}" }

# Clean rebuild.
rebuild: clean build

# Build, then launch the app.
[macos]
run: build
    open "{{build_dir}}/Terminal/CowTerm.app"

[windows]
run: build
    & "{{build_dir}}\Terminal\CowTerm.exe"

[linux]
run: build
    "./{{build_dir}}/Terminal/CowTerm"

# Build and run with popup tracing on. Reproduce the lazygit popup (Ctrl+A i),
# then read /tmp/cowterm-popup.log — it records the popup's grid size, what the
# command drew, and whether/why it closed. Quit the normal instance first so
# they don't share a daemon.
[macos]
debug-popup: build
    open -n --env COWTERM_POPUP_DEBUG=1 "{{build_dir}}/Terminal/CowTerm.app"

# Build, then install the app to the usual place for this OS.
[macos]
install: build
    rm -rf "/Applications/CowTerm.app"
    cp -R "{{build_dir}}/Terminal/CowTerm.app" "/Applications/CowTerm.app"
    codesign --verify --deep --strict --verbose=2 "/Applications/CowTerm.app"
    @echo "Installed CowTerm.app to /Applications"

[windows]
install: build
    New-Item -ItemType Directory -Force -Path "$env:LOCALAPPDATA\Programs\CowTerm" | Out-Null
    Copy-Item -Force "{{build_dir}}\Terminal\CowTerm.exe" "$env:LOCALAPPDATA\Programs\CowTerm"
    Copy-Item -Force "{{build_dir}}\Terminal\CowTermDaemon.exe" "$env:LOCALAPPDATA\Programs\CowTerm"
    $exe = "$env:LOCALAPPDATA\Programs\CowTerm\CowTerm.exe"; $ws = New-Object -ComObject WScript.Shell; @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs')) | ForEach-Object { $s = $ws.CreateShortcut((Join-Path $_ 'CowTerm.lnk')); $s.TargetPath = $exe; $s.WorkingDirectory = (Split-Path $exe); $s.IconLocation = $exe; $s.Description = 'CowTerm'; $s.Save() }; Write-Host "Added Desktop and Start Menu shortcuts"
    ie4uinit.exe -show
    Write-Host "Installed CowTerm to $env:LOCALAPPDATA\Programs\CowTerm"

# The Linux backends aren't wired up in CMake yet (Terminal/CMakeLists.txt
# only branches on APPLE/WIN32), so this is here for parity and untested.
[linux]
install: build
    mkdir -p "$HOME/.local/bin"
    cp "{{build_dir}}/Terminal/CowTerm" "$HOME/.local/bin/cowterm"
    cp "{{build_dir}}/Terminal/CowTermDaemon" "$HOME/.local/bin/CowTermDaemon"
    @echo "Installed to ~/.local/bin"
