# The iPhone app's build stamp: the time and short hash of the last commit that
# changed anything the phone app is built from. The phone sends it in hello;
# the Mac app compares it with its own and notifies when the phone is behind.
#
#   cowterm_add_phone_build_stamp_library(CowTermPhoneBuildStamp)
#   #include <ThisBuildsPhoneStamp.h>
#   term::getThisBuildsPhoneStamp(); // empty in a tree without git
#
#   cmake -DSOURCE_DIRECTORY=<checkout> -DPRINT=ON -P cmake/PhoneBuildStamp.cmake
#
# Whole-repo HEAD would move with every Mac-only commit and nag constantly.

# What iOS/CMakeLists.txt compiles, links and embeds (Lib without the two
# libraries the phone build skips, the two Terminal files and the fonts it
# borrows), and the build files and lock that decide how.
set(phoneBuildStampPaths
        iOS
        Lib
        ":(exclude)Lib/PushRelay"
        ":(exclude)Lib/FolderWatch"
        Terminal/NotifyUserPrompt.cpp
        Terminal/NotifyUserPrompt.h
        Terminal/PasteConfirmation.cpp
        Terminal/PasteConfirmation.h
        Terminal/Resources
        CMakeLists.txt
        cmake
        lockstep.lock)

if (CMAKE_SCRIPT_MODE_FILE)
    set(commitSeconds 0)
    set(commitHash "")
    set(commitLine "")

    # A source tarball has no .git, and must not pick up an enclosing checkout's commit.
    if (EXISTS "${SOURCE_DIRECTORY}/.git")
        execute_process(
                COMMAND git --no-optional-locks log -1 "--format=%ct %h %ci" -- ${phoneBuildStampPaths}
                WORKING_DIRECTORY "${SOURCE_DIRECTORY}"
                OUTPUT_VARIABLE commitLine
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
    endif ()

    if (commitLine MATCHES "^([0-9]+) ([0-9a-f]+) (.*)$")
        set(commitSeconds "${CMAKE_MATCH_1}")
        set(commitHash "${CMAKE_MATCH_2}")
        set(commitDate "${CMAKE_MATCH_3}")
    endif ()

    if (PRINT)
        if (commitHash)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${commitSeconds}-${commitHash} (committed ${commitDate})")
        else ()
            execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "no stamp: ${SOURCE_DIRECTORY} is not a git checkout")
        endif ()

        return()
    endif ()

    # file(CONFIGURE) leaves an unchanged file untouched, so nothing recompiles or relinks.
    file(CONFIGURE OUTPUT "${OUTPUT}" @ONLY CONTENT [[
#include "ThisBuildsPhoneStamp.h"

std::optional<term::PhoneBuildStamp> term::getThisBuildsPhoneStamp()
{
    return parsePhoneBuildStamp("@commitSeconds@-@commitHash@");
}
]])
    return()
endif ()

function(cowterm_add_phone_build_stamp_library libraryName)
    set(generatedDirectory "${CMAKE_CURRENT_BINARY_DIR}/${libraryName}")
    set(generatedSource "${generatedDirectory}/ThisBuildsPhoneStamp.cpp")

    file(CONFIGURE OUTPUT "${generatedDirectory}/ThisBuildsPhoneStamp.h" CONTENT [[
#pragma once

#include <Multiplexer/PhoneBuildStamp.h>

namespace term
{
std::optional<PhoneBuildStamp> getThisBuildsPhoneStamp();
}
]])

    # Runs on every build, so a new commit shows without reconfiguring.
    add_custom_target(${libraryName}Stamp
            BYPRODUCTS "${generatedSource}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}"
                    "-DOUTPUT=${generatedSource}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            COMMENT "Stamping the iPhone app's build"
            VERBATIM)

    add_library(${libraryName} STATIC "${generatedSource}")
    add_dependencies(${libraryName} ${libraryName}Stamp)
    target_include_directories(${libraryName} PUBLIC "${generatedDirectory}")
    target_link_libraries(${libraryName} PUBLIC CowTerm::Multiplexer)
    set_default_target_setting(${libraryName})
endfunction()
