cmake_minimum_required(VERSION 3.31)
project(CowTerm VERSION 0.1.0 LANGUAGES C CXX)

# CowTerm was extracted from the eacp monorepo (Apps/Terminal) into a
# standalone product. It pulls eacp itself in as a CPM dependency rather than
# living inside the eacp source tree; everything the app used from the monorepo
# build (the eacp-* targets and the eacp_* CMake helpers) comes from that
# package.

# The app carries Objective-C++ sources (Cocoa glyph atlas + notifications).
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
# The IPC::Messenger layer the session daemon and DaemonClient ride on lives
# on the jp/fancy-terminal branch (with the Windows Channel/Lock backends);
# it hasn't merged to eacp main yet, so pin the branch that actually has it.
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY jamierpond/eacp
        GIT_TAG jp/fancy-terminal
        OPTIONS
            "EACP_ENABLE_EXAMPLES OFF"
            "EACP_ENABLE_TESTS OFF")

# eacp appends its CMake helper directory to CMAKE_MODULE_PATH only within its
# own directory scope, so re-add it here: the app subdirectories call
# find_package(ResEmbed) (via eacp_set_gui_subsystem) and need the Find modules.
list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# eacp runs eacp_default_setup() only when it is the root project. We are the
# root now, so reproduce the parts the app relies on: the macOS bundle
# Info.plist template that set_default_target_setting() stamps onto the app
# (NSHighResolutionCapable et al.), and the deployment target. eacp_setup_apple
# is a macro keyed off CMAKE_CURRENT_SOURCE_DIR, so it can't be reused verbatim
# from here; point the plist var straight at eacp's template instead.
if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(EACP_MACOS_PLIST "${eacp_SOURCE_DIR}/CMake/macOSBundleInfo.plist.in"
            CACHE INTERNAL "eacp macOS bundle Info.plist template")
endif ()

# eacp's C++20 modules are precompiled; skip module scanning as eacp does.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_subdirectory(Daemon)
add_subdirectory(Terminal)

# Regression tests for the POSIX PTY teardown (the quit-hang fix). The Pty
# sources are POSIX-only, so the suite is gated off Windows.
option(COWTERM_ENABLE_TESTS "Build CowTerm unit tests" ON)
if (COWTERM_ENABLE_TESTS AND NOT WIN32)
    enable_testing()
    add_subdirectory(Test)
endif ()
