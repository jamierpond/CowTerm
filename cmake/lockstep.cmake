# lockstep bootstrap: commit this file and load it before project() with
#   set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
# It downloads the pinned lockstep binary and includes the CMake module it writes.
cmake_minimum_required(VERSION 3.25)

set(LOCKSTEP_VERSION 0.6.0)
set(LOCKSTEP_RELEASE_URL https://github.com/jamierpond/lockstep/releases/download)
set(LOCKSTEP_SHA256_macos-arm64 "18aa39ab7881871fb5b9b2c283c18b373d09a49dea8f234c51c8d7187c2d642d")
set(LOCKSTEP_SHA256_macos-x64 "18aa39ab7881871fb5b9b2c283c18b373d09a49dea8f234c51c8d7187c2d642d")
set(LOCKSTEP_SHA256_windows-x64 "80a6a37215d42331b7db814cb602f03805ceb7d03948d38fa338d8e6e24551d5")
set(LOCKSTEP_SHA256_windows-arm64 "6b4ba029b73f63baa1a2655e421257f54dd10fa90d93e053264c224a4623f5b3")
set(LOCKSTEP_SHA256_linux-x64 "88d8e06ec8763e45f512c5e35c25cecf0939a27d27f1cf3fd9d1618f34cb16d1")
set(LOCKSTEP_SHA256_linux-arm64 "782dd3b53b5a7cd6efe27ccefeb6159d2e38e350efce82dae38b098b5349407a")

set(LOCKSTEP_BOOTSTRAP "${CMAKE_CURRENT_LIST_FILE}")
set(LOCKSTEP_CACHE "$ENV{LOCKSTEP_CACHE}" CACHE PATH "The shared lockstep cache")
if (DEFINED ENV{LOCKSTEP_EXECUTABLE}) # the environment wins over a stale cache entry
    set(LOCKSTEP_EXECUTABLE "$ENV{LOCKSTEP_EXECUTABLE}" CACHE FILEPATH "A lockstep binary to run" FORCE)
endif ()

if (NOT LOCKSTEP_EXECUTABLE)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR}" host)
    string(REGEX REPLACE "^darwin" "macos" host "${host}")
    string(REGEX REPLACE "(x86_64|amd64)$" "x64" host "${host}")
    string(REGEX REPLACE "aarch64$" "arm64" host "${host}")
    set(cache "$ENV{HOME}/.cache/lockstep")
    if (LOCKSTEP_CACHE)
        set(cache "${LOCKSTEP_CACHE}")
    elseif (CMAKE_HOST_WIN32)
        set(cache "$ENV{LOCALAPPDATA}/lockstep")
    elseif (NOT "$ENV{XDG_CACHE_HOME}" STREQUAL "")
        set(cache "$ENV{XDG_CACHE_HOME}/lockstep")
    endif ()
    set(asset "lockstep-${LOCKSTEP_VERSION}-${host}${CMAKE_HOST_EXECUTABLE_SUFFIX}")
    set(LOCKSTEP_EXECUTABLE "${cache}/bin/${LOCKSTEP_VERSION}/${asset}")
    if (NOT EXISTS "${LOCKSTEP_EXECUTABLE}")
        file(DOWNLOAD "${LOCKSTEP_RELEASE_URL}/v${LOCKSTEP_VERSION}/${asset}"
                "${LOCKSTEP_EXECUTABLE}.part" EXPECTED_HASH SHA256=${LOCKSTEP_SHA256_${host}})
        file(CHMOD "${LOCKSTEP_EXECUTABLE}.part" FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
        file(RENAME "${LOCKSTEP_EXECUTABLE}.part" "${LOCKSTEP_EXECUTABLE}")
    endif ()
endif ()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env "LOCKSTEP_EXECUTABLE=${LOCKSTEP_EXECUTABLE}"
        "${LOCKSTEP_EXECUTABLE}" cmake-module
        "--output=${CMAKE_BINARY_DIR}/_lockstep/lockstep-module.cmake" COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_BINARY_DIR}/_lockstep/lockstep-module.cmake")
