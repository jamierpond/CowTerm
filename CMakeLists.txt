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
# Pinned to upstream eacp develop at 80b93a9 plus the fork's stale-mouse-target
# fix (e4573f9, eyalamirmusic/eacp PR #38, still open), carried on
# jamierpond/eacp branch cowterm/stale-mouse-targets-2. Without it,
# View::removeSubview clears hoveredView/mouseDownTarget for direct children
# only, so removing a nested subtree while hovered leaves the root pointing at
# a destroyed view and the next mouseExited is a use-after-free. Point back at
# eyalamirmusic/eacp `develop` (not `main`, which lags it) once #38 lands.
# Provides eacp-text (GlyphAtlas.h) and the IPC::Messenger layer the session
# daemon and DaemonClient ride on.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY jamierpond/eacp
        GIT_TAG 9b7924329f1d940bcfde1d78b9d63d5ff6912f7c
        OPTIONS
            "EACP_ENABLE_EXAMPLES OFF"
            "EACP_ENABLE_TESTS OFF")

# eacp appends its CMake helper directory to CMAKE_MODULE_PATH only within its
# own directory scope, so re-add it here: the app subdirectories call
# find_package(ResEmbed) (via eacp_set_gui_subsystem) and need the Find modules.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

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

# Regression tests for the POSIX PTY teardown (the quit-hang fix). The Pty
# sources are POSIX-only, so the suite is gated off Windows.
option(COWTERM_ENABLE_TESTS "Build CowTerm unit tests" ON)
if (COWTERM_ENABLE_TESTS AND NOT WIN32)
    enable_testing()
    add_subdirectory(Test)
endif ()
