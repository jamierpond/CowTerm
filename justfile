# CowTerm dev commands. Recipes are a line or two of cmake; the Linux and
# Windows ones run under the dependency wrapper in `deps` below.

# On Windows, run recipe bodies through PowerShell (the OS-specific recipes
# below use PowerShell syntax); macOS/Linux use the default sh. Bypass, because
# a stock Windows install runs no scripts at all, scripts/with-msvc.ps1 included.
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# Override on the command line, e.g. `just build_type=Debug build`.
build_dir  := "build"
generator  := "Ninja"
build_type := "Release"

# A Linux build needs the Wayland, text and libcurl development packages that
# eacp looks for with pkg-config, plus a compiler and ninja. scripts/with-linux-deps.sh
# uses the system's own if they are installed and otherwise borrows them from the
# pinned dev shell in flake.nix, so the recipes below work either way.
#
# A Windows build needs the MSVC environment (cl, link, the SDK paths) that a
# Developer PowerShell sets up, because Ninja runs the compiler directly;
# scripts/with-msvc.ps1 brings it in from a plain shell. macOS needs neither.
deps := if os() == "linux" { "scripts/with-linux-deps.sh" } else if os() == "windows" { "scripts/with-msvc.ps1" } else { "" }

# Show the recipe list by default.
default:
    @just --list

# CowTermApp, the always-run target that refreshes the bundled daemon, exists
# only on macOS; elsewhere the executable target copies the daemon in POST_BUILD.
[doc('Configure + build the app; CowTermDaemon builds with it and is bundled alongside')]
[macos]
build: configure
    {{deps}} cmake --build {{build_dir}} --target CowTermApp

[doc('Configure + build the app; CowTermDaemon builds with it and is bundled alongside')]
[windows]
build: configure
    {{deps}} cmake --build {{build_dir}} --target CowTerm

[doc('Configure + build the app; CowTermDaemon builds with it and is bundled alongside')]
[linux]
build: configure
    {{deps}} cmake --build {{build_dir}} --target CowTerm

[doc('Build everything, then run every test')]
test: configure
    {{deps}} cmake --build {{build_dir}}
    {{deps}} ctest --test-dir {{build_dir}} --output-on-failure

[doc('Build and run the tests of the project file-change watch and the session order it feeds')]
test-file-changes: configure
    {{deps}} cmake --build {{build_dir}} --target CowTermFileChangeTests CowTermFolderWatchTests CowTermPaneSizeOwnerTests CowTermDaemonPhoneProtocolTests
    {{deps}} ctest --test-dir {{build_dir}} --output-on-failure -R "FileChangeRules|RecursiveFolderWatcher|SessionOverview|ActivityReports|DaemonPhoneProtocol"

# Run just test-file-changes on another machine (Linux, macOS or Windows over git-bash) against this working tree; --remove deletes its folder there
test-file-changes-on-host host *options:
    scripts/test-recipe-on-host.sh {{host}} test-file-changes {{options}}

[doc("Build and run the tests of the phone's build stamp: the hello that carries it, the daemon listing its clients, and when the Mac notifies")]
test-phone-build-stamp: configure
    {{deps}} cmake --build {{build_dir}} --target CowTermPaneSizeOwnerTests CowTermDaemonPhoneProtocolTests
    {{deps}} ctest --test-dir {{build_dir}} --output-on-failure -R "PhoneBuildStamp|DaemonClientSizeOwner|DaemonPhoneProtocol"

# Run just test-phone-build-stamp on another machine (Linux, macOS or Windows over git-bash) against this working tree; --remove deletes its folder there
test-phone-build-stamp-on-host host *options:
    scripts/test-recipe-on-host.sh {{host}} test-phone-build-stamp {{options}}

[doc("Build everything, then run clang-tidy (eacp's checks) over CowTerm's sources; fails on any finding")]
[unix]
tidy: configure
    {{deps}} cmake --build {{build_dir}}
    {{deps}} scripts/tidy.sh {{build_dir}}

# Every rebuild is a new ad-hoc identity to macOS, so it keeps the
# old privacy grant switched on in System Settings while refusing it to the
# new binary. Resetting before launch makes the app ask again for real. The
# app does the reset itself (`CowTerm --reset-permissions [service]`, the same
# code as CowTerm > Reset all permissions…), reading its own bundle id.
[doc('Build, then launch the app')]
[macos]
run: build
    @echo "[run] resetting Accessibility for CowTerm"
    -@"{{build_dir}}/Terminal/CowTerm.app/Contents/MacOS/CowTerm" --reset-permissions Accessibility
    scripts/open-cowterm-cleanly.sh "{{build_dir}}/Terminal/CowTerm.app"

[doc('Build, then launch the app')]
[windows]
run: build
    & "{{build_dir}}\Terminal\CowTerm.exe"

# Absolute path on purpose. eacp decides standalone-vs-plugin by comparing
# /proc/self/exe against the path dladdr reports for the module, and dladdr
# reports the path as invoked -- so launching as ./build/... makes the two
# differ, eacp concludes it is a plugin, and main() returns without ever
# running the event loop (the app exits silently in milliseconds).
#
# Under {{deps}} like the build, because on this OS the app needs its
# dependencies at *run* time too. volk dlopens libvulkan.so.1 when the first
# GPUView is created, and a binary the dev shell built cannot see a
# /usr/lib one -- so without the shell around it the loader is simply absent,
# every view falls back to rendering off-screen, and the window opens empty.
# When the deps are installed system-wide instead, with-linux-deps.sh execs
# straight through and this costs nothing.
[doc('Build, then launch the app')]
[linux]
run: build
    {{deps}} "$(pwd)/{{build_dir}}/Terminal/CowTerm"

# On Windows a running CowTerm locks its exe against overwrite but not rename,
# so the copy falls back to renaming the live exe aside (*.old, cleaned up next
# run); a running CowTerm and its daemon keep going, shells intact, until you
# switch with Kill everything & quit (scripts/install-windows.ps1 says so).
# On macOS the bundle goes to ~/.local/libexec/cowterm. Also links `ct` (the
# CowTerm executable's CLI mode) into ~/.local/bin. On Windows `ct` is a ct.cmd
# there instead: CowTerm.exe is a GUI executable, which a shell would not wait
# for, and a batch file makes it.
[doc('Build, then install the normal app, without line coverage, to the usual place for this OS')]
[macos]
install-without-coverage: build
    mkdir -p "$HOME/.local/bin" "$HOME/.local/libexec/cowterm"
    rm -rf "$HOME/.local/libexec/cowterm/CowTerm.app"
    cp -R "{{build_dir}}/Terminal/CowTerm.app" "$HOME/.local/libexec/cowterm/CowTerm.app"
    ln -sf "$HOME/.local/libexec/cowterm/CowTerm.app/Contents/MacOS/CowTerm" "$HOME/.local/bin/ct"
    @echo "Installed CowTerm.app to ~/.local/libexec/cowterm (ct -> ~/.local/bin/ct)"

[doc('Build, then install the normal app, without line coverage, to the usual place for this OS')]
[windows]
install-without-coverage: build
    scripts/install-windows.ps1 "{{build_dir}}" "$env:LOCALAPPDATA\Programs\CowTerm"
    $exe = "$env:LOCALAPPDATA\Programs\CowTerm\CowTerm.exe"; $ws = New-Object -ComObject WScript.Shell; @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs')) | ForEach-Object { $s = $ws.CreateShortcut((Join-Path $_ 'CowTerm.lnk')); $s.TargetPath = $exe; $s.WorkingDirectory = (Split-Path $exe); $s.IconLocation = $exe; $s.Description = 'CowTerm'; $s.Save() }; Write-Host "Added Desktop and Start Menu shortcuts"
    ie4uinit.exe -show
    $exe = "$env:LOCALAPPDATA\Programs\CowTerm\CowTerm.exe"; $bin = "$HOME\.local\bin"; New-Item -ItemType Directory -Force -Path $bin | Out-Null; Set-Content -Encoding ascii "$bin\ct.cmd" "@if `"%~1`"==`"`" (`"$exe`" help) else (`"$exe`" %*)"; Write-Host "Added ct ($bin\ct.cmd)"
    Write-Host "Installed CowTerm to $env:LOCALAPPDATA\Programs\CowTerm"

# Terminal/CMakeLists.txt now has a LINUX branch (notifications via notify-send,
# deletes via the freedesktop trash spec), so this installs a real build.
#
# The .desktop file and the icon are what make the shell show "CowTerm" and the
# app's own icon rather than a placeholder: it matches them to the window by the
# app_id the window carries (Main.cpp's WindowOptions::appId), which is why the
# file is named for that id and repeats it in StartupWMClass.
# The executables go in libexec rather than on PATH, with a generated launcher
# in front of them, because on this OS the app needs its build dependencies at
# run time too -- see scripts/write-launcher.sh. They have to sit together in one
# directory either way: DaemonClient.cpp finds CowTermDaemon next to
# /proc/self/exe, so separating them is what leaves the app falling back to
# in-process shells that die with the window.
[doc('Build, then install the normal app, without line coverage, to the usual place for this OS')]
[linux]
install-without-coverage: build
    mkdir -p "$HOME/.local/bin" "$HOME/.local/libexec/cowterm" "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
    cp "{{build_dir}}/Terminal/CowTerm" "$HOME/.local/libexec/cowterm/CowTerm"
    cp "{{build_dir}}/Terminal/CowTermDaemon" "$HOME/.local/libexec/cowterm/CowTermDaemon"
    {{deps}} scripts/write-launcher.sh "$HOME/.local/libexec/cowterm/CowTerm" "$HOME/.local/bin/cowterm"
    # Earlier installs put both executables directly on PATH; leaving the old
    # daemon there would shadow nothing but would still confuse a `which`.
    rm -f "$HOME/.local/bin/CowTermDaemon"
    # CowTerm was com.eacp.cowterm before it moved to audio.pond.cowterm.
    rm -f "$HOME/.local/share/applications/com.eacp.cowterm.desktop" "$HOME"/.local/share/icons/hicolor/*/apps/com.eacp.cowterm.*
    # One size per directory, because a shell picks the nearest and scales it:
    # handing it only 512 leaves a 32px panel icon downscaled from a picture
    # eight times too big, which is where the detail turns to mush.
    for size in 16 24 32 48 64 128 256 512; do \
        mkdir -p "$HOME/.local/share/icons/hicolor/${size}x${size}/apps"; \
        magick "Terminal/Icon.png" -resize "${size}x${size}" "$HOME/.local/share/icons/hicolor/${size}x${size}/apps/audio.pond.cowterm.png"; \
    done
    cp "Terminal/Icon.svg" "$HOME/.local/share/icons/hicolor/scalable/apps/audio.pond.cowterm.svg"
    sed "s|^Exec=cowterm$|Exec=$HOME/.local/bin/cowterm|" "Terminal/audio.pond.cowterm.desktop" > "$HOME/.local/share/applications/audio.pond.cowterm.desktop"
    -update-desktop-database "$HOME/.local/share/applications" 2>/dev/null
    -gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null
    @echo "Installed to ~/.local/bin, with a desktop entry and icon"

# For now `install` puts in the line-coverage build (see the line coverage
# section below), so a normal day's use is counted. Clang builds it, so on
# Windows `install` stays the normal build.
[doc('For now the line-coverage build: build it and install it, keeping the old and new programs for coverage-report')]
[unix]
install: coverage-build
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS keep
    just build_dir={{coverage_build_dir}} install-without-coverage
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS keep

[doc('Build, then install the app to the usual place for this OS')]
[windows]
install: install-without-coverage

[doc('Build and install ct + cowtermd (headless) on a remote host over ssh, for remote sessions')]
install-remote host:
    scripts/install-remote.sh "{{host}}"

[doc('Build and install the headless ct + cowtermd on this machine (what a remote host runs)')]
[unix]
install-headless:
    scripts/install-headless.sh

# LOCKSTEP_MODE is cached, so the second configure puts the build tree back in
# locked mode.
[doc("Move every dependency to its branch or tag's current commit and rewrite lockstep.lock")]
update-deps:
    {{deps}} cmake -S . -B {{build_dir}} -G "{{generator}}" -DCMAKE_BUILD_TYPE={{build_type}} -DLOCKSTEP_MODE=update
    {{deps}} cmake -S . -B {{build_dir}} -G "{{generator}}" -DCMAKE_BUILD_TYPE={{build_type}} -DLOCKSTEP_MODE=locked

[doc('Delete the build tree')]
clean:
    cmake -E rm -rf {{build_dir}}

# lockstep fetches exactly the commits in lockstep.lock and stops if a
# dependency's files don't match it.
[doc('Configure the CMake build tree; safe to run again')]
configure:
    {{deps}} cmake -S . -B {{build_dir}} -G "{{generator}}" -DCMAKE_BUILD_TYPE={{build_type}}

[doc('Clean rebuild')]
rebuild: clean build

# Reproduce the lazygit popup (Ctrl+A i), then read /tmp/cowterm-popup.log: it
# records the popup's grid size, what the command drew, and whether/why it
# closed. Quit the normal instance first so they don't share a daemon.
[doc('Build and run with popup tracing on')]
[macos]
debug-popup: build
    scripts/open-cowterm-cleanly.sh "{{build_dir}}/Terminal/CowTerm.app" -n --env COWTERM_POPUP_DEBUG=1

[doc('clang-format every tracked source in place')]
[unix]
format:
    git ls-files '*.h' '*.cpp' '*.mm' | xargs clang-format -i

# ---- CowTerm for iPhone (iOS/, docs/iOSDesign.md) ----

smallest_iphone := "iPhone 17e"
screenshot_folder := "build-ios-sim/screenshots"

# Build the iPhone app for the simulator
ios-build *cmake_build_arguments:
    scripts/build-ios-simulator.sh {{cmake_build_arguments}}

# Install and launch the iPhone app, and open the Simulator window on it
ios-run device=smallest_iphone *app_arguments:
    scripts/run-ios-simulator.sh "{{device}}" {{app_arguments}}
    scripts/show-ios-simulator.sh "{{device}}"

# Build, launch, and open the Simulator window
ios device=smallest_iphone *app_arguments: ios-build
    scripts/run-ios-simulator.sh "{{device}}" {{app_arguments}}
    scripts/show-ios-simulator.sh "{{device}}"

# Open the Simulator window on an app that is already running
ios-show device=smallest_iphone:
    scripts/show-ios-simulator.sh "{{device}}"

# Open a real pane from a host in the simulator and save a screenshot of it
ios-host destination screenshot=(screenshot_folder / "host.png"):
    scripts/run-ios-against-host.sh {{destination}} {{screenshot}}

# Screenshot every pane screen state and the overview on the smallest and largest iPhone
ios-screenshots:
    scripts/screenshot-ios-pane-screen.sh
    scripts/screenshot-ios-overview.sh {{screenshot_folder}}

# Render the Mac's palette, switcher and window strip in each host's colour at four window shapes, with colour-blind and greyscale views; pass a cowterm.json for its host colours
render-host-colours output="build/renders/host-colours" config="":
    scripts/render-host-colours.sh "{{output}}" {{config}}

# Write protanopia, deuteranopia and greyscale versions of a screenshot beside it
simulate-colour-vision screenshot:
    scripts/simulate-colour-vision.py {{screenshot}}

# Check the phone taking a pane's size and giving it back, on a test daemon on a host
ios-test-size-owner destination folder:
    scripts/run-ios-size-owner-test.sh {{destination}} {{folder}}

# Check the phone gives a protocol 4 daemon's throwaway panes their size back on leaving them and in the background
ios-test-size-return destination device="iPhone 17 Pro":
    scripts/check-ios-size-return.sh {{destination}} "{{device}}"

# Check the overview against a test daemon on a host
ios-test-overview destination folder:
    scripts/run-ios-overview-test.sh {{destination}} {{folder}}

# Check the unread mark on the phone against a test daemon on a host: the unread session first, and opening it reads it
ios-test-unread destination folder prefix=(screenshot_folder / "unread") device="iPhone 17 Pro":
    scripts/check-ios-unread.sh {{destination}} {{folder}} {{prefix}} "{{device}}"

# Check that the phone moves between two throwaway panes on a host's test daemon
ios-test-pane-switching *arguments:
    scripts/check-ios-pane-switching.sh {{arguments}}

# Check that the phone draws its stored screen of a host's pane before the link is up
ios-test-stored-screen *arguments:
    scripts/check-ios-stored-screen.sh {{arguments}}

# Press the diff button on throwaway panes in throwaway /tmp folders on a host: dv, the sentence when it cannot run, and its pane killed
ios-test-diff-view *arguments:
    scripts/check-ios-diff-view.sh {{arguments}}

# Review a throwaway repository on a host through dv in the simulator: a comment saved with Ctrl+s, one saved with Save, submit, and print the round dv wrote
ios-test-diff-view-review destination="jamiepond@macprorack" prefix="build-ios-sim/diff-view-review":
    scripts/check-ios-diff-view-review.sh "{{destination}}" "{{prefix}}"

[doc("Open a throwaway project that has no session on <user@host> from the phone's list, screenshot the list and the pane, and check the pane is gone after cleanup")]
ios-test-project-opening destination prefix=(screenshot_folder / "project") device="iPhone 17 Pro":
    scripts/check-ios-project-opening.sh {{destination}} {{prefix}} "{{device}}"

# Send the running app to the background and back, and print the lifecycle lines it logged
ios-test-lifecycle *arguments:
    scripts/check-ios-lifecycle.sh {{arguments}}

# Resize a program 150 → 49 → 150 columns on a separate daemon and report what its scrollback holds
ios-test-resize-scrollback *arguments:
    scripts/resize-scrollback-test.py {{arguments}}

# Reach a host's daemon through the in-app ssh link from the command line
ios-probe-ssh *arguments:
    scripts/probe-embedded-ssh.sh {{arguments}}

# Build the iPhone app for a real phone, install it and launch it (the phone plugged in and unlocked)
ios-device phone="pond" *app_arguments:
    scripts/install-ios-device.sh "{{phone}}" {{app_arguments}}

# Give the phone a key, the hosts and their host keys, and put its key on this Mac, tamby and macprorack
ios-pair phone="pond":
    scripts/pair-ios-device.sh "{{phone}}"

# This Mac's name as the phone dials it and the Mac colours its sessions by (hostName in ~/.config/cowterm.json)
this-machine-name:
    scripts/print-this-machine-name.sh

# Take the phone's key off this Mac, tamby and macprorack
ios-unpair phone="pond":
    scripts/unpair-ios-device.sh "{{phone}}"

# Type into a throwaway pane on this Mac and measure how soon it shows on the phone
ios-device-test-live-output phone="pond":
    scripts/check-ios-device-live-output.sh "{{phone}}"

# Have Jamie press each key-row key on the phone into a throwaway `cat -v` pane and print what arrived
ios-device-test-key-row destination="jamiepond@macprorack" phone="pond":
    scripts/check-ios-device-key-row.sh "{{destination}}" "{{phone}}"

# Have Jamie tap a file in dv on the phone and print dv's screen before and after the tap
ios-device-test-diff-view-tap destination="jamiepond@macprorack" phone="pond":
    scripts/check-ios-device-diff-view-tap.sh "{{destination}}" "{{phone}}"

# Install this checkout's headless ct and cowtermd on a host; its running daemon keeps running
install-headless-on-host host:
    scripts/install-headless-on-host.sh {{host}}

# Open a CowTerm app with the environment the Dock gives it, never the caller's
open-cowterm-cleanly app *open_arguments:
    scripts/open-cowterm-cleanly.sh "{{app}}" {{open_arguments}}

# Restart a host's CowTerm daemon on the installed build: ends programs running in its panes (ask first)
restart-daemon-on-host host:
    scripts/restart-daemon-on-host.sh {{host}}

# Restart this Mac's CowTerm app and daemon, detached from every pane: ends programs running in its panes (ask first)
restart-cowterm-on-this-mac:
    scripts/restart-cowterm-on-this-mac.sh

# Restart only this Mac's CowTerm app on the installed build; the daemon and every program in every pane keep running
restart-cowterm-app-on-this-mac:
    scripts/restart-cowterm-on-this-mac.sh --app-only

# Show on a separate daemon instance on a host what an update does to running shells
check-update-keeps-shells destination folder:
    scripts/check-update-keeps-shells.sh {{destination}} {{folder}}

# Print the log the app wrote on the phone on its last launch, or with "previous" the one before
ios-device-log phone="pond" which="":
    scripts/read-ios-device-log.sh "{{phone}}" {{which}}

# Write the iPhone app's icons, every size iOS asks for, from the Mac app's Terminal/Icon.svg
ios-app-icon:
    scripts/make-ios-app-icon.sh

# Ask for the push relay's 1Password service-account token and store it on the relay host, readable only by its user
push-relay-store-token host="jamiepond@macprorack":
    scripts/store-push-relay-token.sh {{host}}

# Add the Claude Code hooks that tell a pane's daemon when Claude finishes or asks (ct pane-state --claude-hook), here or on user@host
claude-hooks-add host="":
    scripts/claude-hooks.sh add {{host}}

# Remove exactly those hooks, here or on user@host
claude-hooks-remove host="":
    scripts/claude-hooks.sh remove {{host}}

# Build and install the push relay on the relay host as a user service that survives a reboot
push-relay-install host="jamiepond@macprorack":
    scripts/install-push-relay.sh {{host}}

# Restart the push relay on the relay host (after storing its token, say)
push-relay-restart host="jamiepond@macprorack":
    ssh {{host}} 'systemctl --user restart cowterm-push-relay && sleep 3 && journalctl --user -u cowterm-push-relay -n 5 --no-pager -o cat'

# Print the push relay's last log lines: registrations, notices and Apple's answers
push-relay-log host="jamiepond@macprorack" lines="40":
    ssh {{host}} 'journalctl --user -u cowterm-push-relay -n {{lines}} --no-pager -o short-iso'

# Point a machine's daemon at the push relay and name it as the phone dials it (takes effect at the daemon's next start)
push-relay-set-host host name *relay:
    scripts/set-push-relay-on-host.sh {{host}} {{name}} {{relay}}

# Tell the iPhone app where the push relay is (the phone plugged in and unlocked)
ios-set-push-relay phone="pond" relay="http://macprorack:8742":
    scripts/set-ios-push-relay.sh "{{phone}}" "{{relay}}"

# Remake iOS/Resources/ba-ding.caf, the push notification's two-note sound
ios-make-push-sound:
    scripts/make-ba-ding-sound.py

# Print the push relay's address as daemons use it (its full tailnet name)
push-relay-address:
    scripts/find-push-relay-address.sh

# Ring the phone from a throwaway pane on a separate daemon on the relay host, and print the relay's log of it
push-check-on-phone destination="jamiepond@macprorack":
    scripts/check-push-on-phone.sh {{destination}}

# Print the activity report a running daemon gives (no host: this Mac's): how long ago each pane was typed into and had a file change in its project
print-activity host="":
    scripts/print-activity.py {{host}}

# Print the iPhone app's build stamp for this checkout: the last commit that changed what the phone is built from
print-phone-build-stamp:
    scripts/print-phone-build-stamp.sh

# Print the clients a running daemon has (no host: this Mac's) and the build stamp each sent, the phone's among them
print-connected-clients host="":
    scripts/print-connected-clients.py {{host}}

# Walk a throwaway pane on a host's test daemon through unread and read with Claude Code's own hook command
check-unread-on-host destination="jamiepond@macprorack" folder="$HOME/tmp/cowterm-unread-check":
    scripts/build-headless-instance-on-host.sh {{destination}} '{{folder}}'
    scripts/check-unread-on-host.py {{destination}} '{{folder}}/bin/ct'

# Run just test on another Mac against this working tree (--ios: and just ios-build; --committed: HEAD only; --remove: delete its folder there)
test-on-host host name *options:
    scripts/test-working-tree-on-host.sh {{host}} {{name}} {{options}}

# In the simulator: a tap reaches the pane after the overview closes, and a drag scrolls it after the menu closes
ios-check-hidden-views device="iPhone 17e":
    scripts/check-ios-hidden-views.sh "{{device}}"

# Print one line per thing the push relay needs on its host, ready or not, and what to run for each that is not
push-relay-check-ready host="jamiepond@macprorack":
    scripts/check-push-relay-ready.sh {{host}}

[doc('List the crash reports nobody has marked fixed here and on other machines (list [HOST ...]), or mark one fixed (mark-fixed HOST ID); reads the script from build, which just test configures')]
crash-reports *arguments:
    python3 build/PondCrashReports/crash-reports.py --app CowTerm --program CowTerm --program CowTermDaemon {{arguments}}

# ---- Line coverage: which lines of CowTerm's own code a normal day runs ----

coverage_build_dir := "build-coverage"
coverage_cmake_arguments := "-DPOND_LINE_COVERAGE=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
# PondDev's line-coverage scripts, which configuring with POND_LINE_COVERAGE copies here.
coverage_scripts := coverage_build_dir / "PondLineCoverage"
line_coverage := coverage_scripts / "line-coverage.py"
coverage_check_headless := if os() == "linux" { "ON" } else { "OFF" }

# line-coverage.py's settings for CowTerm. Recipes expand it unquoted, so the
# shell leaves each ~ for line-coverage.py to expand on the machine it reads.
export COWTERM_COVERAGE_SETTINGS := "--app CowTerm --program CowTerm --program CowTermDaemon --program ct --program cowtermd --installed-in ~/.local/libexec/cowterm/CowTerm.app/Contents/MacOS --installed-in ~/.local/libexec/cowterm --installed-in ~/.local/libexec/cowterm-remote --own-code-folder Lib --own-code-folder Terminal --own-code-folder Daemon --own-code-folder Remote --not-own-code-folder iOS --not-own-code-folder Tests --not-own-code-folder Bench --not-own-code-folder Fuzz"

# check-line-coverage.sh's arguments for CowTerm: a throwaway daemon reached through `ct bridge`.
coverage_check_arguments := "--instance-variable COWTERM_INSTANCE --target CowTermDaemon --target CowTermCli --program Daemon/CowTermDaemon=cowtermd --program Remote/ct=ct --start 'scripts/probe-bridge.py --local \"$1/ct\" --keep' --kill cowtermd"

[private]
coverage-configure:
    {{deps}} cmake -S . -B {{coverage_build_dir}} -G "{{generator}}" -DCMAKE_BUILD_TYPE={{build_type}} {{coverage_cmake_arguments}}

[doc('Build the app with line-coverage counters, in build-coverage')]
coverage-build: coverage-configure
    just build_dir={{coverage_build_dir}} build

[doc("Install a line-coverage headless ct and cowtermd on a host in place of its normal ones; its running daemon keeps running")]
coverage-install-headless-on-host host: coverage-configure
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS keep {{host}}
    scripts/install-headless-on-host.sh {{host}} '$HOME/tmp/cowterm-coverage-install' build-headless-coverage {{coverage_cmake_arguments}}
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS keep {{host}}

[doc('Print the code that never ran in any build, largest first, from this machine and every collected host')]
coverage-report *arguments: coverage-configure
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS report {{arguments}}

[doc("Copy a host's line-coverage counts and its programs here, for coverage-report")]
coverage-collect-from-host host *arguments: coverage-configure
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS collect {{host}} {{arguments}}

[doc('Delete the line-coverage counts and kept programs here, or on user@host')]
coverage-reset host="" *arguments: coverage-configure
    {{line_coverage}} $COWTERM_COVERAGE_SETTINGS reset {{host}} {{arguments}}

[doc("Kill a throwaway instrumented daemon (instance coverage-check) without a clean exit and show its counts survived")]
coverage-check: coverage-configure
    {{deps}} {{coverage_scripts}}/check-line-coverage.sh --coverage-arguments "$COWTERM_COVERAGE_SETTINGS" --build-directory {{coverage_build_dir}} {{coverage_check_arguments}} -- -DCOWTERM_HEADLESS={{coverage_check_headless}} {{coverage_cmake_arguments}}

[doc("The same check on user@host, detached there, then collect and report its counts here")]
coverage-check-on-host host: coverage-configure
    {{coverage_scripts}}/check-line-coverage-on-host.sh {{host}} CowTerm scripts/with-linux-deps.sh "$COWTERM_COVERAGE_SETTINGS" {{coverage_check_arguments}} -- -DCOWTERM_HEADLESS=ON {{coverage_cmake_arguments}}

[doc("Build ct twice, the second with printVersion changed, and show coverage-report counts a function run only under the first build as run")]
coverage-check-across-builds: coverage-configure
    {{deps}} {{coverage_scripts}}/check-line-coverage-across-builds.sh --coverage-arguments "$COWTERM_COVERAGE_SETTINGS" --instance-variable COWTERM_INSTANCE --target CowTermCli --program Remote/ct --source Remote/CtMain.cpp --function printVersion --marker '"second build"' --substitution 's/int printVersion\(const Arguments&\)\n\{\n/int printVersion(const Arguments& arguments)\n{\n    if (arguments.size() > 1)\n        std::puts("second build");\n/' --first-run version --second-run help -- -DCOWTERM_HEADLESS=ON {{coverage_cmake_arguments}}
