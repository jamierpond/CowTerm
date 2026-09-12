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
# Ad-hoc by default: no code-signing identity is needed to build locally. The
# cost is that macOS privacy grants (screen capture) are re-prompted after each
# rebuild, since an ad-hoc signature's designated requirement is its cdhash.
allow_adhoc_signing := "ON"

# Default: show the recipe list.
default:
    @just --list

# Configure the CMake build tree (idempotent; first run fetches deps via CPM).
configure:
    cmake -S . -B {{build_dir}} -G "{{generator}}" -DCMAKE_BUILD_TYPE={{build_type}} -DCOWTERM_MACOS_SIGNING_IDENTITY="{{signing_identity}}" -DCOWTERM_MACOS_ALLOW_ADHOC_SIGNING={{allow_adhoc_signing}}

# Build the app; CowTermDaemon builds as a dependency and is bundled alongside
# it. CowTermApp (the always-run packaging + signing target) exists only on
# macOS; elsewhere the executable target copies the daemon via POST_BUILD.
[macos]
build: configure
    cmake --build {{build_dir}} --target CowTermApp

[windows]
build: configure
    cmake --build {{build_dir}} --target CowTerm

[linux]
build: configure
    cmake --build {{build_dir}} --target CowTerm

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

# Absolute path on purpose. eacp decides standalone-vs-plugin by comparing
# /proc/self/exe against the path dladdr reports for the module, and dladdr
# reports the path as invoked -- so launching as ./build/... makes the two
# differ, eacp concludes it is a plugin, and main() returns without ever
# running the event loop (the app exits silently in milliseconds).
[linux]
run: build
    "$(pwd)/{{build_dir}}/Terminal/CowTerm"

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

# Terminal/CMakeLists.txt now has a LINUX branch (notifications via notify-send,
# deletes via the freedesktop trash spec), so this installs a real build.
#
# The .desktop file and the icon are what make the shell show "CowTerm" and the
# app's own icon rather than a placeholder: it matches them to the window by the
# app_id the window carries (Main.cpp's WindowOptions::appId), which is why the
# file is named for that id and repeats it in StartupWMClass.
[linux]
install: build
    mkdir -p "$HOME/.local/bin" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
    cp "{{build_dir}}/Terminal/CowTerm" "$HOME/.local/bin/cowterm"
    cp "{{build_dir}}/Terminal/CowTermDaemon" "$HOME/.local/bin/CowTermDaemon"
    # One size per directory, because a shell picks the nearest and scales it:
    # handing it only 512 leaves a 32px panel icon downscaled from a picture
    # eight times too big, which is where the detail turns to mush.
    for size in 16 24 32 48 64 128 256 512; do \
        mkdir -p "$HOME/.local/share/icons/hicolor/${size}x${size}/apps"; \
        magick "Terminal/Icon.png" -resize "${size}x${size}" "$HOME/.local/share/icons/hicolor/${size}x${size}/apps/com.eacp.cowterm.png"; \
    done
    cp "Terminal/Icon.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/com.eacp.cowterm.svg"
    sed "s|^Exec=cowterm$|Exec=$HOME/.local/bin/cowterm|" "Terminal/com.eacp.cowterm.desktop" > "$HOME/.local/share/applications/com.eacp.cowterm.desktop"
    -update-desktop-database "$HOME/.local/share/applications" 2>/dev/null
    -gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null
    @echo "Installed to ~/.local/bin, with a desktop entry and icon"
