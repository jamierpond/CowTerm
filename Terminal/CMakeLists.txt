# Local persistence (MRU stamps, session restore). Fetched here rather than
# at the top level because only the CowTerm app uses it; emberstore reuses
# the eacp/Miro targets already in this build.
CPMAddPackage(
        NAME emberstore
        GITHUB_REPOSITORY tamber-inc/emberstore
        GIT_TAG main
)

# On macOS the CowTermApp build target is an always-run packaging target: it
# refreshes the bundled daemon and signs the completed app even when only the
# daemon, plist, icon, or another resource changed. Keep the executable target
# separate because Ninja reserves the output-name alias `CowTerm` for it.
if (APPLE)
    set(COWTERM_EXECUTABLE_TARGET CowTermExecutable)
else ()
    set(COWTERM_EXECUTABLE_TARGET CowTerm)
endif ()

# The emulator and the terminal widget live in Lib/ (CowTerm::Terminal);
# what remains here is the product around them — sessions, splits, palette,
# switcher, daemon client, tray.
add_executable(${COWTERM_EXECUTABLE_TARGET}
        Main.cpp
        AppShell.cpp
        ClaudeHud.cpp
        Command.cpp
        Config.cpp
        GitWorktree.cpp
        MruStore.cpp
        Palette.cpp
        PrDashboard.cpp
        Popup.cpp
        Projects.cpp
        Session.cpp
        SessionView.cpp
        Switcher.cpp
        TrayController.cpp
        DaemonClient.cpp)

# Per-platform notifications and trash.
if (APPLE)
    target_sources(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            Notifier-macOS.mm
            Trash-macOS.mm)

    target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            "-framework UserNotifications"
            "-framework Foundation")
elseif (WIN32)
    target_sources(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            Notifier-Windows.cpp
            Trash-Windows.cpp)

    # The dashboard and switcher sources hold Unicode literals (powerline
    # glyphs); MSVC assumes the ANSI codepage without this.
    target_compile_options(${COWTERM_EXECUTABLE_TARGET} PRIVATE /utf-8)

    target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            d2d1
            dwrite
            windowscodecs
            user32
            ole32
            shell32)
elseif (LINUX)
    # notify-send for desktop notifications, the freedesktop.org trash spec for
    # deletes: both are plain filesystem/process work, so nothing to link.
    target_sources(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            Notifier-Linux.cpp
            Trash-Linux.cpp)
endif ()

# eacp-ui is the portable component/painter tier: one ComponentHost (a GPUView)
# draws the whole overlay tree through eacp-text's glyph batcher, with no
# per-platform branches. The overlays use it rather than Graphics::Context,
# which is the native 2D tier and exists only on macOS and Windows.
target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE
        CowTerm::Terminal eacp-sprites eacp-text eacp-ui eacp-network emberstore)

# Stamp the git SHA into a generated header on every build so the running app
# can report exactly which commit it came from (window title + startup log).
set(_version_in  ${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in)
set(_version_out ${CMAKE_CURRENT_BINARY_DIR}/generated/CowTermVersion.h)
add_custom_target(CowTermVersion
        BYPRODUCTS ${_version_out}
        COMMAND ${CMAKE_COMMAND}
                -DSRC=${CMAKE_SOURCE_DIR}
                -DIN=${_version_in}
                -DOUT=${_version_out}
                -DCOWTERM_PROJECT_VERSION=${PROJECT_VERSION}
                -P ${CMAKE_SOURCE_DIR}/cmake/GitVersion.cmake
        COMMENT "Stamping git version")
add_dependencies(${COWTERM_EXECUTABLE_TARGET} CowTermVersion)
target_include_directories(${COWTERM_EXECUTABLE_TARGET} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated)

# JetBrains Mono (OFL) rides inside the binary; registered with the platform
# font system at startup, so the default font needs no install step.
res_embed_add(${COWTERM_EXECUTABLE_TARGET} DIRECTORY Resources)

set(BUNDLE_ID "com.eacp.cowterm")
set(DAEMON_BUNDLE_ID "${BUNDLE_ID}.daemon")

set_target_properties(${COWTERM_EXECUTABLE_TARGET} PROPERTIES
        OUTPUT_NAME "CowTerm"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CowTerm"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(${COWTERM_EXECUTABLE_TARGET})
set_default_target_setting(${COWTERM_EXECUTABLE_TARGET})
eacp_set_app_icon(${COWTERM_EXECUTABLE_TARGET}
        IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Icon.png")

# The daemon rides beside the GUI executable. CowTermApp is deliberately always
# out of date: this keeps a daemon-only rebuild from leaving a stale bundled
# copy, and ensures resource-only changes are sealed by a fresh outer signature.
if (APPLE)
    add_dependencies(${COWTERM_EXECUTABLE_TARGET} CowTermDaemon)

    set(COWTERM_PACKAGE_COMMAND
            ${CMAKE_COMMAND}
            -DAPP_PATH=$<TARGET_BUNDLE_DIR:${COWTERM_EXECUTABLE_TARGET}>
            -DDAEMON_SOURCE_PATH=$<TARGET_FILE:CowTermDaemon>
            -DDAEMON_PATH=$<TARGET_FILE_DIR:${COWTERM_EXECUTABLE_TARGET}>/CowTermDaemon
            -DAPP_IDENTIFIER=${BUNDLE_ID}
            -DDAEMON_IDENTIFIER=${DAEMON_BUNDLE_ID}
            -DSIGNING_IDENTITY=${COWTERM_MACOS_SIGNING_IDENTITY}
            -DALLOW_ADHOC=${COWTERM_MACOS_ALLOW_ADHOC_SIGNING}
            -P ${CMAKE_SOURCE_DIR}/cmake/SignMacOSApp.cmake)

    # A direct executable-target build still receives a valid signature.
    add_custom_command(TARGET ${COWTERM_EXECUTABLE_TARGET} POST_BUILD
            COMMAND ${COWTERM_PACKAGE_COMMAND}
            COMMENT "Refreshing and signing CowTerm.app after linking"
            VERBATIM)

    # Normal builds use this always-run target, which also covers changes that
    # do not relink the GUI executable.
    add_custom_target(CowTermApp ALL
            COMMAND ${COWTERM_PACKAGE_COMMAND}
            DEPENDS ${COWTERM_EXECUTABLE_TARGET} CowTermDaemon
            COMMENT "Refreshing and signing CowTerm.app"
            VERBATIM)
else ()
    # Preserve the existing target and post-build layout off macOS.
    add_dependencies(CowTerm CowTermDaemon)
    add_custom_command(TARGET CowTerm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:CowTermDaemon>
                    $<TARGET_FILE_DIR:CowTerm>)
endif ()
