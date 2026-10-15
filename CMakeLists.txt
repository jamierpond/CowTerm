cmake_minimum_required(VERSION 3.31)
project(CowTerm VERSION 0.1.0 LANGUAGES C CXX)

# CowTerm was extracted from the eacp monorepo (Apps/Terminal) into a
# standalone product. It pulls eacp itself in as a CPM dependency rather than
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
# before the CPM packages so their targets inherit it.
if (MSVC)
    add_compile_options(/EHsc)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)

# The framework: Core, Network, the GPU/Sprites stack, and the eacp_* CMake
# helpers (res_embed_add, eacp_set_gui_subsystem, eacp_set_app_icon, ...) that
# the app's CMakeLists call. Examples/tests are top-level-only in eacp, so they
# stay off here. Point CPM at a local checkout with -DCPM_eacp_SOURCE=/path.
# Pinned to jamierpond/eacp main at 9a124ea: upstream eacp develop (52313a8)
# plus three fixes CowTerm depends on, all in eyalamirmusic/eacp PR #141. Two
# are the Windows keyboard: keys reach the focused view rather than only the
# window's content view, and KeyEvent::charactersIgnoringModifiers is filled
# in, which the Ctrl+A leader matches on. The third is the stale-mouse-target
# fix (once PR #38): without it View::removeSubview clears hoveredView/
# mouseDownTarget for direct children only, so removing a nested subtree while
# hovered leaves the root pointing at a destroyed view and the next
# mouseExited is a use-after-free. Point back at eyalamirmusic/eacp `develop`
# (not `main`, which lags it) once #141 lands.
# Provides eacp-text (GlyphAtlas.h) and the IPC::Messenger layer the session
# daemon and DaemonClient ride on.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY jamierpond/eacp
        GIT_TAG 9a124ea146fa48e406eeb2120dd4cd8888d961a4
        OPTIONS
            "EACP_ENABLE_EXAMPLES OFF"
            "EACP_ENABLE_TESTS OFF")

# eacp appends its CMake helper directory to CMAKE_MODULE_PATH only within its
# own directory scope, so re-add it here: the app subdirectories call
# find_package(ResEmbed) (via eacp_set_gui_subsystem) and need the Find modules.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# Publishes CowTerm's panes and ctrl+a f places to Rudder (the app switcher)
# as contexts. Only Rudder's client library is built; CowTerm behaves exactly
# as before when Rudder isn't running, and on a platform Rudder has no
# transport for yet its client is a silent stub. Work against a local Rudder
# checkout with:
#   cmake -B build -DCPM_Rudder_SOURCE=$HOME/projects/Rudder
CPMAddPackage(
        NAME Rudder
        GITHUB_REPOSITORY jamierpond/Rudder
        GIT_TAG main
        OPTIONS "RUDDER_CLIENT_ONLY ON"
)

# This cache entry exists on every platform so cross-platform `just` recipes
# can pass it without producing an "unused variable" warning. It only affects
# the macOS packaging target.
set(COWTERM_MACOS_SIGNING_IDENTITY "" CACHE STRING
        "Certificate name or SHA-1 used to sign CowTerm; empty auto-detects")

# eacp runs eacp_default_setup() only when it is the root project. We are the
# root now, so reproduce the deployment target and point its target helper at
# CowTerm's own plist. A terminal is the responsible application for many
# privacy requests made by its child commands, so the generic eacp plist is not
# sufficient here.
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(EACP_MACOS_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Terminal/Info.plist.in"
            CACHE INTERNAL "CowTerm macOS bundle Info.plist template" FORCE)

    option(COWTERM_MACOS_ALLOW_ADHOC_SIGNING
            "Allow disposable macOS builds whose privacy grants reset" OFF)
endif ()

# eacp's C++20 modules are precompiled; skip module scanning as eacp does.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The libraries (see Lib/CMakeLists.txt), then the product built on them.
add_subdirectory(Lib)
add_subdirectory(Daemon)
add_subdirectory(Terminal)

# Unit tests, including the PTY teardown regressions (the quit-hang fix) for
# both the POSIX PTY and ConPTY.
option(COWTERM_ENABLE_TESTS "Build CowTerm unit tests" ON)
if (COWTERM_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(Test)
endif ()
