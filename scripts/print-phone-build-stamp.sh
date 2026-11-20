#!/bin/sh
# Prints the iPhone app's build stamp for this checkout: the time and short
# hash of the last commit that changed what the phone app is built from
# (cmake/PhoneBuildStamp.cmake lists the paths). A phone with an older stamp
# than the installed Mac app gets a desktop notification on the Mac.
#
#   scripts/print-phone-build-stamp.sh
set -eu
cd "$(dirname "$0")/.."
cmake -DSOURCE_DIRECTORY="$(pwd)" -DPRINT=ON -P cmake/PhoneBuildStamp.cmake
