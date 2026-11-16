cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)

project(CowTerm VERSION 0.1.0 LANGUAGES C CXX)

# CowTerm was extracted from the eacp monorepo (Apps/Terminal) into a
# standalone product. It pulls eacp itself in as a lockstep dependency rather than
# living inside the eacp source tree; everything the app used from the monorepo
# build (the eacp-* targets and the eacp_* CMake helpers) comes from that
# package.

# The app carries Objective-C++ sources (notifications, trash).
if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# eacp links its static libraries into a GUI executable; every static dep must
# be position-independent, matching eacp's own top-level build.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC compiles the standard library's try/catch (e.g. <chrono>) without unwind
# semantics unless told otherwise, raising C4530 — a hard error in the deps that
# build warnings-as-error (emberstore's /WX). Enable standard C++ exception
# handling for every target, matching the other platforms' Clang default. Set
# before the packages so their targets inherit it.
if (MSVC)
    add_compile_options(/EHsc)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The framework: Core, Network, the GPU/Sprites stack, and the eacp_* CMake
# helpers (res_embed_add, eacp_set_gui_subsystem, eacp_set_app_icon, ...) that
# the app's CMakeLists call. Examples/tests are top-level-only in eacp, so they
# stay off here. Point lockstep at a local checkout with -DLOCKSTEP_eacp_SOURCE=/path.
# eacp's C++20 modules are precompiled; skip module scanning as eacp does. Set
# before eacp is added, so its libraries are built the same way in every pond app.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# eacp develop, which carries PR #141 since it merged: Windows keys reach the
# focused view, KeyEvent::charactersIgnoringModifiers is filled in (the Ctrl+A
# leader matches on it), and removing a hovered nested subtree no longer leaves
# a dangling hoveredView. Not `main`, which lags develop.
# Provides eacp-text (GlyphAtlas.h) and the IPC::Messenger layer the session
# daemon and DaemonClient ride on.
# A remote machine needs only ct and the session daemon (docs/RemoteSessions.md).
# Headless skips eacp's graphics, so the build needs no Wayland or font
# libraries, and skips the app, the fuzzers and the tests.
option(COWTERM_HEADLESS "Build only ct and cowtermd, for a remote machine" OFF)

set(COWTERM_EACP_OPTIONS "EACP_ENABLE_EXAMPLES OFF" "EACP_ENABLE_TESTS OFF")
if (COWTERM_HEADLESS)
    list(APPEND COWTERM_EACP_OPTIONS "EACP_BUILD_GRAPHICS OFF")
endif ()

lockstep_add_package(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG develop
        SYSTEM YES
        PATCHES "${CMAKE_CURRENT_LIST_DIR}/Patches/eacp-hidden-gpuview-releases-frame-memory.patch"
        OPTIONS ${COWTERM_EACP_OPTIONS})

# Crash reports (Crashout): the app and the daemon write one to
# <app support>/<executable>/Crashes/pending/<id>/ when they crash, which
# scripts/crash-reports.py lists. Its handler exists on macOS and Windows only.
lockstep_add_package(
        NAME Crashout
        GITHUB_REPOSITORY jamierpond/Crashout
        GIT_TAG main
        SYSTEM YES)

# eacp appends its CMake helper directory to CMAKE_MODULE_PATH only within its
# own directory scope, so re-add it here: the app subdirectories call
# find_package(ResEmbed) (via eacp_set_gui_subsystem) and need the Find modules.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# Publishes CowTerm's panes and ctrl+a f places to Rudder (the app switcher)
# as contexts. Only Rudder's client library is built; CowTerm behaves exactly
# as before when Rudder isn't running, and on a platform Rudder has no
# transport for yet its client is a silent stub. Work against a local Rudder
# checkout with:
#   cmake -B build -DLOCKSTEP_Rudder_SOURCE=$HOME/projects/Rudder
lockstep_add_package(
        NAME Rudder
        GITHUB_REPOSITORY jamierpond/Rudder
        GIT_TAG main
        OPTIONS "RUDDER_CLIENT_ONLY ON")

# eacp runs eacp_default_setup() only when it is the root project. We are the
# root now, so point its target helper at CowTerm's own plist (the deployment
# target eacp would set is at the top: it only takes effect before project()). A terminal is the responsible application for many
# privacy requests made by its child commands, so the generic eacp plist is not
# sufficient here.
if (APPLE)
    set(EACP_MACOS_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Terminal/Info.plist.in"
            CACHE INTERNAL "CowTerm macOS bundle Info.plist template" FORCE)
endif ()

# Set after the packages, so only CowTerm's own targets build with -Werror.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

# MSVC flags getenv and fopen as deprecated (C4996), which -WX turns into errors.
if (MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif ()

# Local persistence (MRU stamps, session restore, the daemon's saved panes).
# emberstore reuses the eacp/Miro targets already in this build.
lockstep_add_package(
        NAME emberstore
        GITHUB_REPOSITORY tamber-inc/emberstore
        GIT_TAG main)

# Stamp the git SHA into a generated header on every build so ct and the app
# can report exactly which commit they came from (window title + startup log).
set(COWTERM_VERSION_HEADER ${PROJECT_BINARY_DIR}/generated/CowTermVersion.h)
add_custom_target(CowTermVersion
        BYPRODUCTS ${COWTERM_VERSION_HEADER}
        COMMAND ${CMAKE_COMMAND}
                -DSRC=${CMAKE_SOURCE_DIR}
                -DIN=${CMAKE_SOURCE_DIR}/Terminal/Version.h.in
                -DOUT=${COWTERM_VERSION_HEADER}
                -DCOWTERM_PROJECT_VERSION=${PROJECT_VERSION}
                -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        COMMENT "Stamping git version")

# The libraries (see Lib/CMakeLists.txt), then the product built on them.
add_subdirectory(Lib)
add_subdirectory(Daemon)
add_subdirectory(Remote)

if (COWTERM_HEADLESS)
    return()
endif ()

add_subdirectory(Terminal)
add_subdirectory(Fuzz)

# Unit tests, including the PTY teardown regressions (the quit-hang fix) for
# both the POSIX PTY and ConPTY.
option(COWTERM_ENABLE_TESTS "Build CowTerm unit tests" ${PROJECT_IS_TOP_LEVEL})
if (COWTERM_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif ()
