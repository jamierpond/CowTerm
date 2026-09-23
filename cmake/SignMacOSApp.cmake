# Signs CowTerm's nested daemon and then the completed app bundle.
#
# TCC uses a program's designated requirement to remember privacy decisions.
# An ad-hoc signature has a version-specific `cdhash` requirement, so screen
# capture and other grants are lost whenever the executable changes. This
# script therefore requires a certificate-backed identity unless the caller
# explicitly opts into ad-hoc signing.

foreach(required
        APP_PATH
        DAEMON_SOURCE_PATH
        DAEMON_PATH
        APP_IDENTIFIER
        DAEMON_IDENTIFIER)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "SignMacOSApp.cmake requires -D${required}=...")
    endif ()
endforeach ()

if (NOT EXISTS "${APP_PATH}/Contents/Info.plist")
    message(FATAL_ERROR "CowTerm app bundle is incomplete: ${APP_PATH}")
endif ()

if (NOT EXISTS "${DAEMON_SOURCE_PATH}")
    message(FATAL_ERROR "CowTerm daemon has not been built: ${DAEMON_SOURCE_PATH}")
endif ()

find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
find_program(SECURITY_EXECUTABLE security REQUIRED)

# Always refresh the bundled copy before signing. In particular, rebuilding
# only CowTermDaemon must not leave a stale executable in the app bundle.
file(COPY_FILE
        "${DAEMON_SOURCE_PATH}"
        "${DAEMON_PATH}"
        ONLY_IF_DIFFERENT
        INPUT_MAY_BE_RECENT)

set(identity "${SIGNING_IDENTITY}")

# Prefer an Apple Development certificate for local builds. Developer ID is a
# useful fallback, as is any other valid code-signing identity explicitly
# installed by the developer (including a local certificate).
#
# Only identities in unlocked keychains are considered. A locked keychain still
# advertises its identities through `security find-identity`, but codesign then
# fails with errSecInternalComponent (or blocks on an unlock prompt) because it
# cannot reach the private key. Release keychains are commonly kept locked, so
# discovery must not hand the build an identity it cannot actually use.
if (identity STREQUAL "")
    execute_process(
            COMMAND "${SECURITY_EXECUTABLE}" list-keychains -d user
            RESULT_VARIABLE keychain_result
            OUTPUT_VARIABLE keychain_output
            ERROR_VARIABLE keychain_error)

    if (NOT keychain_result EQUAL 0)
        message(FATAL_ERROR
                "Could not list the macOS keychains:\n${keychain_error}")
    endif ()

    string(REGEX MATCHALL "\"[^\"]+\"" keychains "${keychain_output}")

    set(identity_output "")
    set(locked_note "")

    foreach (keychain IN LISTS keychains)
        string(REGEX REPLACE "^\"|\"$" "" keychain "${keychain}")

        # show-keychain-info only succeeds on an unlocked keychain.
        execute_process(
                COMMAND "${SECURITY_EXECUTABLE}" show-keychain-info "${keychain}"
                RESULT_VARIABLE unlocked_result
                OUTPUT_QUIET
                ERROR_QUIET)

        if (NOT unlocked_result EQUAL 0)
            # Worth reporting only if unlocking it would actually help.
            execute_process(
                    COMMAND "${SECURITY_EXECUTABLE}" find-identity -v -p codesigning
                            "${keychain}"
                    OUTPUT_VARIABLE locked_output
                    ERROR_QUIET)
            string(REGEX MATCHALL "\"[^\"]+\"" locked_names "${locked_output}")
            list(REMOVE_DUPLICATES locked_names)
            if (NOT locked_names STREQUAL "")
                string(JOIN ", " names ${locked_names})
                string(APPEND locked_note
                        "  ${names}\n    security unlock-keychain ${keychain}\n")
            endif ()
            continue ()
        endif ()

        execute_process(
                COMMAND "${SECURITY_EXECUTABLE}" find-identity -v -p codesigning
                        "${keychain}"
                RESULT_VARIABLE find_result
                OUTPUT_VARIABLE find_output
                ERROR_QUIET)

        if (find_result EQUAL 0)
            string(APPEND identity_output "${find_output}")
        endif ()
    endforeach ()

    string(REGEX MATCH "\"Apple Development:[^\"]+\"" identity_match
            "${identity_output}")

    if (identity_match STREQUAL "")
        string(REGEX MATCH "\"Developer ID Application:[^\"]+\"" identity_match
                "${identity_output}")
    endif ()

    if (identity_match STREQUAL "")
        string(REGEX MATCH "\"[^\"]+\"" identity_match "${identity_output}")
    endif ()

    string(REGEX REPLACE "^\"|\"$" "" identity "${identity_match}")
endif ()

if (identity STREQUAL "")
    if (NOT locked_note STREQUAL "")
        set(locked_note
                "Signing identities exist, but only in locked keychains. "
                "Unlock one and rebuild:\n${locked_note}")
    endif ()

    if (ALLOW_ADHOC)
        set(identity "-")
        message(WARNING
                "CowTerm is being ad-hoc signed. macOS privacy grants will not "
                "survive a rebuild. Install an Apple Development certificate "
                "and build without COWTERM_MACOS_ALLOW_ADHOC_SIGNING.\n"
                ${locked_note})
    else ()
        message(FATAL_ERROR
                "CowTerm needs a stable macOS code-signing identity so privacy "
                "grants survive rebuilds, but no usable one was found.\n"
                ${locked_note}
                "Create an Apple Development certificate in Xcode under "
                "Settings > Accounts > Manage Certificates, or configure with "
                "-DCOWTERM_MACOS_SIGNING_IDENTITY=<certificate name or SHA-1>.\n"
                "For a deliberately disposable build only, use "
                "-DCOWTERM_MACOS_ALLOW_ADHOC_SIGNING=ON.")
    endif ()
endif ()

if (identity STREQUAL "-" AND NOT ALLOW_ADHOC)
    message(FATAL_ERROR
            "COWTERM_MACOS_SIGNING_IDENTITY=- is unstable ad-hoc signing. "
            "Set COWTERM_MACOS_ALLOW_ADHOC_SIGNING=ON to opt in explicitly.")
endif ()

function(run_codesign)
    execute_process(
            COMMAND "${CODESIGN_EXECUTABLE}" ${ARGN}
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE error)

    if (NOT result EQUAL 0)
        string(JOIN " " rendered_command "${CODESIGN_EXECUTABLE}" ${ARGN})
        message(FATAL_ERROR
                "Code signing failed (${rendered_command}):\n${output}${error}")
    endif ()
endfunction()

# Sign nested code first. `--deep` is intentionally reserved for verification;
# Apple deprecates it for signing because it can apply the wrong identity or
# entitlements to nested code.
run_codesign(
        --force
        --sign "${identity}"
        --identifier "${DAEMON_IDENTIFIER}"
        --timestamp=none
        "${DAEMON_PATH}")

run_codesign(
        --force
        --sign "${identity}"
        --identifier "${APP_IDENTIFIER}"
        --timestamp=none
        "${APP_PATH}")

run_codesign(--verify --deep --strict --verbose=4 "${APP_PATH}")

execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" --display --requirements - "${APP_PATH}"
        RESULT_VARIABLE requirement_result
        OUTPUT_VARIABLE requirement_output
        ERROR_VARIABLE requirement_error)

if (NOT requirement_result EQUAL 0)
    message(FATAL_ERROR
            "Could not read CowTerm's designated requirement:\n"
            "${requirement_output}${requirement_error}")
endif ()

set(requirement "${requirement_output}${requirement_error}")

if (identity STREQUAL "-")
    if (NOT requirement MATCHES "cdhash")
        message(WARNING
                "Expected an ad-hoc cdhash requirement, got:\n${requirement}")
    endif ()
elseif (requirement MATCHES "cdhash")
    message(FATAL_ERROR
            "CowTerm was expected to have a stable certificate-backed "
            "designated requirement, but codesign produced:\n${requirement}")
endif ()

message(STATUS "Signed CowTerm with '${identity}'")
message(STATUS "${requirement}")
