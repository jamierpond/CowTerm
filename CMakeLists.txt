cmake_minimum_required(VERSION 3.31)

# iOS 15 is the oldest iOS eacp supports and the oldest Xcode 27 builds for.
if (CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "")
else ()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif ()
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

# GCC 15.3 bug, not UB in CowTerm: with -flto, -fipa-bit-cp passes a std::max-clamped argument on as 1 (Tests/ClampedRowCount*.cpp reproduce it).
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fno-ipa-bit-cp)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# The framework: Core, Network, the GPU/Sprites stack, and the eacp_* CMake
# helpers (res_embed_add, eacp_set_gui_subsystem, eacp_set_app_icon, ...) that
# the app's CMakeLists call. Examples/tests are top-level-only in eacp, so they
# stay off here. Point lockstep at a local checkout with -DLOCKSTEP_eacp_SOURCE=/path.
# eacp's C++20 modules are precompiled; skip module scanning as eacp does. Set
# before eacp is added, so its libraries are built the same way in every pond app.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# eacp jp/develop: develop plus Jamie's unmerged fixes, one commit each. CowTerm
# needs #268 (a hidden GPUView gives back its frame memory, so hidden sessions
# don't hold GPU surfaces). Move back to develop once #268 merges.
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
        GIT_TAG jp/develop
        SYSTEM YES
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

# An ssh client inside the process (Lib/Multiplexer/EmbeddedSshLink), for the
# iPhone, which cannot run the system ssh. libssh2 does its crypto through
# mbedTLS. The mbedtls-* tags carry the generated sources, so no Python is
# needed to build them.
if (APPLE AND NOT COWTERM_HEADLESS)
    lockstep_add_package(
            NAME mbedtls
            GITHUB_REPOSITORY Mbed-TLS/mbedtls
            GIT_TAG mbedtls-3.6.8
            SYSTEM YES
            OPTIONS "ENABLE_TESTING OFF" "ENABLE_PROGRAMS OFF" "MBEDTLS_FATAL_WARNINGS OFF"
                    "USE_SHARED_MBEDTLS_LIBRARY OFF" "USE_STATIC_MBEDTLS_LIBRARY ON"
                    "DISABLE_PACKAGE_CONFIG_AND_INSTALL OFF")

    # lockstep answers libssh2's find_package(MbedTLS) with the mbedtls package
    # above, so FindMbedTLS.cmake never runs: its result variables are given
    # here. CMP0126 NEW keeps CRYPTO_BACKEND when libssh2 declares it a cache entry.
    lockstep_add_package(
            NAME libssh2
            GITHUB_REPOSITORY libssh2/libssh2
            GIT_TAG libssh2-1.11.1
            SYSTEM YES
            OPTIONS "CRYPTO_BACKEND mbedTLS" "BUILD_SHARED_LIBS OFF" "BUILD_STATIC_LIBS ON"
                    "BUILD_EXAMPLES OFF" "BUILD_TESTING OFF" "ENABLE_ZLIB_COMPRESSION OFF"
                    "LINT OFF" "PICKY_COMPILER OFF" "CMAKE_POLICY_DEFAULT_CMP0126 NEW"
                    "MBEDTLS_FOUND ON"
                    "MBEDTLS_INCLUDE_DIRS ${mbedtls_SOURCE_DIR}/include"
                    "MBEDTLS_LIBRARIES mbedcrypto")
endif ()

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

# ct, the daemon and the app report one version: <semver>+<commit>, from Rudder.
pond_add_version_library(CowTermVersion)

# The libraries (see Lib/CMakeLists.txt), then the product built on them.
add_subdirectory(Lib)

# iOS forbids starting processes, so the phone build has none of the daemon,
# ct, the Mac app, the fuzzers or the tests: only the libraries and the iPhone
# app (docs/iOSApp.md).
if (IOS)
    add_subdirectory(iOS)
    return()
endif ()

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
