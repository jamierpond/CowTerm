eacp_with_ios_branches := env("EACP_SOURCE", home_directory() / "projects/eacp-ios-integration")
smallest_iphone := "iPhone 17e"
screenshot_folder := "build-ios-sim/screenshots"

default:
    @just --list

# Build the Mac app and run every test
test *ctest_arguments:
    scripts/build-and-test-mac.sh {{ctest_arguments}}

# Build the iPhone app for the simulator, against the eacp checkout that has the iOS branches
ios-build *cmake_build_arguments:
    EACP_SOURCE={{eacp_with_ios_branches}} scripts/build-ios-simulator.sh {{cmake_build_arguments}}

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

# Check the phone taking a pane's size and giving it back, on a test daemon on a host
ios-test-size-owner destination folder:
    scripts/run-ios-size-owner-test.sh {{destination}} {{folder}}

# Check the phone gives a protocol 4 daemon's throwaway panes their size back on leaving them and in the background
ios-test-size-return destination device="iPhone 17 Pro":
    scripts/check-ios-size-return.sh {{destination}} "{{device}}"

# Check the overview against a test daemon on a host
ios-test-overview destination folder:
    scripts/run-ios-overview-test.sh {{destination}} {{folder}}

# Check that the phone moves between two throwaway panes on a host's test daemon
ios-test-pane-switching *arguments:
    scripts/check-ios-pane-switching.sh {{arguments}}

# Check that the phone draws its stored screen of a host's pane before the link is up
ios-test-stored-screen *arguments:
    scripts/check-ios-stored-screen.sh {{arguments}}

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
    EACP_SOURCE={{eacp_with_ios_branches}} scripts/install-ios-device.sh "{{phone}}" {{app_arguments}}

# Give the phone a key, the hosts and their host keys, and put its key on this Mac, tamby and macprorack
ios-pair phone="pond":
    scripts/pair-ios-device.sh "{{phone}}"

# Take the phone's key off this Mac, tamby and macprorack
ios-unpair phone="pond":
    scripts/unpair-ios-device.sh "{{phone}}"

# Type into a throwaway pane on this Mac and measure how soon it shows on the phone
ios-device-test-live-output phone="pond":
    scripts/check-ios-device-live-output.sh "{{phone}}"

# Print the log the app wrote on the phone on its last launch, or with "previous" the one before
ios-device-log phone="pond" which="":
    scripts/read-ios-device-log.sh "{{phone}}" {{which}}
