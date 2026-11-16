# On macOS the CowTermApp build target is an always-run packaging target: it
# refreshes the bundled daemon and signs the completed app even when only the
# daemon, plist, icon, or another resource changed. Keep the executable target
# separate because Ninja reserves the output-name alias `CowTerm` for it.
if (APPLE)
    set(COWTERM_EXECUTABLE_TARGET CowTermExecutable)
else ()
    set(COWTERM_EXECUTABLE_TARGET CowTerm)
endif ()

# The libraries live in Lib/ (CowTerm::TerminalView draws a pane,
# CowTerm::Multiplexer keeps panes in the daemon and on remote hosts); what
# remains here is the app's own UI — sessions, splits, palette, switcher,
# popups, notifications, tray.
add_executable(${COWTERM_EXECUTABLE_TARGET}
        Main.cpp
        AppShell.cpp
        Cli.cpp
        Command.cpp
        GitBranch.cpp
        LeaderKeys.cpp
        NotifyUserPrompt.cpp
        NotifyUserServer.cpp
        OverlayPanel.cpp
        Palette.cpp
        PaletteRanking.cpp
        PaletteUsageStore.cpp
        PasteConfirmation.cpp
        CowTermPermissions.cpp
        Popup.cpp
        RudderContexts.cpp
        RudderPublisher.cpp
        SearchField.cpp
        Session.cpp
        SessionView.cpp
        Switcher.cpp
        TrayController.cpp
        PaneTerminalConfig.cpp)

# Per-platform halves of ct, shell commands, the places watcher,
# notifications and trash. Privacy grants come from Rudder's PermissionsPanel.
if (APPLE)
    target_sources(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            Command-Posix.cpp
            DirWatcher-macOS.mm
            Notifier-macOS.mm)

    target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            "-framework UserNotifications"
            "-framework Foundation"
            "-framework CoreGraphics"
            "-framework ApplicationServices"
            "-framework AppKit")
elseif (WIN32)
    target_sources(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            Cli-Windows.cpp
            Command-Windows.cpp
            DirWatcher-Windows.cpp
            Notifier-Windows.cpp)

    # The dashboard and switcher sources hold Unicode literals (powerline
    # glyphs); MSVC assumes the ANSI codepage without this.
    target_compile_options(${COWTERM_EXECUTABLE_TARGET} PRIVATE /utf-8)

    target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            d2d1
            gdi32
            dwrite
            windowscodecs
            user32
            ole32
            shell32)
elseif (LINUX)
    # notify-send for desktop notifications, the freedesktop.org trash spec for
    # deletes: both are plain filesystem/process work, so nothing to link.
    target_sources(${COWTERM_EXECUTABLE_TARGET} PRIVATE
            Command-Posix.cpp
            DirWatcher-Linux.cpp
            Notifier-Linux.cpp)
endif ()

# eacp-ui is the portable component/painter tier: one ComponentHost (a GPUView)
# draws the whole overlay tree through eacp-text's glyph batcher, with no
# per-platform branches. The overlays use it rather than Graphics::Context,
# which is the native 2D tier and exists only on macOS and Windows.
target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE
        CowTerm::TerminalView CowTerm::Multiplexer eacp-sprites eacp-text eacp-ui eacp-network emberstore PermissionsPanel
        PondApp RudderClient FrecencySearch SearchKeys SessionLog CowTermVersion)

# JetBrains Mono (OFL) rides inside the binary; registered with the platform
# font system at startup, so the default font needs no install step.
res_embed_add(${COWTERM_EXECUTABLE_TARGET} DIRECTORY Resources)

set(BUNDLE_ID "audio.pond.cowterm")

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

if (APPLE OR WIN32)
    eacp_enable_crash_reporting(${COWTERM_EXECUTABLE_TARGET}
            SYMBOLS_DIR ${PROJECT_BINARY_DIR}/symbols)
    target_link_libraries(${COWTERM_EXECUTABLE_TARGET} PRIVATE eacp-crash-faults)
endif ()

# The daemon rides beside the GUI executable. CowTermApp is deliberately always
# out of date so a daemon-only rebuild never leaves a stale bundled copy.
if (APPLE)
    add_dependencies(${COWTERM_EXECUTABLE_TARGET} CowTermDaemon)

    add_custom_target(CowTermApp ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:CowTermDaemon>
                    $<TARGET_FILE_DIR:${COWTERM_EXECUTABLE_TARGET}>/CowTermDaemon
            DEPENDS ${COWTERM_EXECUTABLE_TARGET} CowTermDaemon
            COMMENT "Refreshing the daemon inside CowTerm.app"
            VERBATIM)
else ()
    # Preserve the existing target and post-build layout off macOS.
    add_dependencies(CowTerm CowTermDaemon)
    add_custom_command(TARGET CowTerm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:CowTermDaemon>
                    $<TARGET_FILE_DIR:CowTerm>)
endif ()
