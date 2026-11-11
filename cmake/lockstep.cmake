# lockstep bootstrap: commit this file and load it before project() with
#   set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
# It downloads the pinned lockstep binary and includes the CMake module it writes.
cmake_minimum_required(VERSION 3.25)

set(LOCKSTEP_VERSION 0.7.0)
set(LOCKSTEP_RELEASE_URL https://github.com/jamierpond/lockstep/releases/download)
set(LOCKSTEP_SHA256_macos-arm64 "4cc122cb95bc3926c566d7b101f3f421d6f0256c19a235080f96687ca410321c")
set(LOCKSTEP_SHA256_macos-x64 "4cc122cb95bc3926c566d7b101f3f421d6f0256c19a235080f96687ca410321c")
set(LOCKSTEP_SHA256_windows-x64 "ca32e59bd29314e3ab25677bf04780faa476270381c21335da62ae7c7fd354ee")
set(LOCKSTEP_SHA256_windows-arm64 "10682b913e0851d5deb1b3daaf5f5271ac24f7a7fa31c759db8dbab8571aad02")
set(LOCKSTEP_SHA256_linux-x64 "d210eabf3fb9b3ce57cb1e988daddc0d1cb75959b54b83731f0f76ba44b9d86c")
set(LOCKSTEP_SHA256_linux-arm64 "819715fabd3570f7ef84d904166fd7fefb35f8e0efa6273006d339c6677d610d")

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
