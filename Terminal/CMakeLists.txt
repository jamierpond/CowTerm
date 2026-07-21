# Local persistence (MRU stamps, session restore). Fetched here rather than
# at the top level because only the CowTerm app uses it; emberstore reuses
# the eacp/Miro targets already in this build.
CPMAddPackage(
        NAME emberstore
        GITHUB_REPOSITORY tamber-inc/emberstore
        GIT_TAG main
)

add_executable(CowTerm
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
        SessionCommand.cpp
        SessionView.cpp
        Switcher.cpp
        TermScreen.cpp
        TermParser.cpp
        TerminalView.cpp
        TrayController.cpp
        DaemonClient.cpp
        GlyphAtlas.cpp
        RemoteHud.cpp
        Web/WebSocket.cpp
        Web/ScreenSerializer.cpp
        Web/WebGateway.cpp
        Web/GatewayClient.cpp)

# Portable core (parser, screen, sessions, palette) + per-platform PTY,
# glyph raster and notifications.
if (APPLE)
    target_sources(CowTerm PRIVATE
            Pty-Posix.cpp
            Notifier-macOS.mm
            Trash-macOS.mm)

    target_link_libraries(CowTerm PRIVATE "-framework UserNotifications" "-framework Foundation")
elseif (WIN32)
    target_sources(CowTerm PRIVATE
            Pty-Windows.cpp
            Notifier-Windows.cpp
            Trash-Windows.cpp)

    # The parser and screen sources hold Unicode literals (box drawing,
    # powerline glyphs); MSVC assumes the ANSI codepage without this.
    target_compile_options(CowTerm PRIVATE /utf-8)

    target_link_libraries(CowTerm PRIVATE
            d2d1
            dwrite
            windowscodecs
            user32
            ole32
            shell32)
endif ()

target_link_libraries(CowTerm PRIVATE eacp-sprites eacp-text eacp-network emberstore)

# The session daemon rides inside the bundle, next to the app binary, so
# the client can launch it with no install step.
add_dependencies(CowTerm CowTermDaemon)
add_custom_command(TARGET CowTerm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:CowTermDaemon> $<TARGET_FILE_DIR:CowTerm>)

# JetBrains Mono (OFL) and the web UI page ride inside the binary; the fonts
# are registered with the platform font system at startup and the page is
# served by the web gateway, so neither needs an install step.
#
# The file list is globbed and passed as explicit DEPENDS: res_embed_add's
# directory mode discovers files at build time and writes a depfile naming
# only what the *previous* run saw, so a newly added resource — or an edit
# to one that run missed — never re-triggers the generator, and the app
# silently serves a stale (or absent) asset. CONFIGURE_DEPENDS re-globs when
# files come and go; DEPENDS re-runs the generator when their contents change.
file(GLOB COWTERM_RESOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*")

res_embed_add(CowTerm DIRECTORY Resources DEPENDS ${COWTERM_RESOURCES})

set(BUNDLE_ID "com.eacp.cowterm")

set_target_properties(CowTerm PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "CowTerm"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID})

eacp_set_gui_subsystem(CowTerm)
set_default_target_setting(CowTerm)
eacp_set_app_icon(CowTerm IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Icon.png")
