# lockstep bootstrap: commit this file and load it before project() with
#   set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
# It downloads the pinned lockstep binary and includes the CMake module it writes.
cmake_minimum_required(VERSION 3.25)

set(LOCKSTEP_VERSION 0.4.3)
set(LOCKSTEP_RELEASE_URL https://github.com/jamierpond/lockstep/releases/download)
set(LOCKSTEP_SHA256_macos-arm64 "56a67bdc84acbf112e4bf4cd7f02518afb5c15bb5add3476039c227d4444d704")
set(LOCKSTEP_SHA256_macos-x64 "56a67bdc84acbf112e4bf4cd7f02518afb5c15bb5add3476039c227d4444d704")
set(LOCKSTEP_SHA256_windows-x64 "937a27f491c14c3353c6671c8b8878d3eababf8af1ff4435d924601a79af3050")
set(LOCKSTEP_SHA256_windows-arm64 "440778b15ff1c75c55d9a5d5e5585175b9c3069a0bca760b3245485966c96554")
set(LOCKSTEP_SHA256_linux-x64 "7ce28b25ca7500817538c7084554c2da845a30d45a89feb61af002aaf6919504")
set(LOCKSTEP_SHA256_linux-arm64 "2de9cc91611e5dc1e02d44c59729c4a5ba0a2dd71c96c6b5d4dbb2bee9042542")

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
