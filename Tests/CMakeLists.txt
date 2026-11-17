# Unit tests. NanoTest is a header-plus-tiny-lib framework that generates a
# main() and registers each test case with CTest (see nano_add_executable).
lockstep_add_package(
        NAME NanoTest
        GITHUB_REPOSITORY eyalamirmusic/NanoTest
        GIT_TAG main)

# The PTY teardown, with each platform's own cases beside the shared ones.
# Pty-Posix.cpp has no eacp dependencies, so it compiles straight into the test
# binary rather than through the library target; ConPTY's Pty-Windows.cpp leans
# on eacp's string helpers, so there the binary links the library instead.
if (WIN32)
    nano_add_executable(CowTermTests
            SOURCES
                PtyTests.cpp
                PtyTests-Windows.cpp
            TARGETS CowTerm::Shell)
else ()
    nano_add_executable(CowTermTests
            SOURCES
                PtyTests.cpp
                PtyTests-Posix.cpp
                ../Lib/Shell/Pty-Posix.cpp)
endif ()

target_include_directories(CowTermTests PRIVATE ../Lib/Shell)

# The parser and screen do pull in eacp (Rgb and friends), so these link the
# library rather than compiling its sources in the way the PTY tests do.
nano_add_executable(CowTermScreenTests
        SOURCES
            ScreenColorTests.cpp
            ParserFuzzRegressionTests.cpp
            TerminalModeTests.cpp
            ClipboardOscTests.cpp
            ReflowTests.cpp
            TerminalStateTests.cpp
            DirtyRowTests.cpp
        TARGETS CowTerm::Emulator)

# Replays every recording in Fuzz/Corpus (the fuzzer's seeds and every input
# it once crashed on) and checks each screen against Fuzz/Snapshots.txt.
nano_add_executable(CowTermFuzzCorpusTests
        SOURCES
            FuzzCorpusReplayTests.cpp
            FuzzCorpusStateTests.cpp
        TARGETS CowTerm::Emulator)

target_include_directories(CowTermFuzzCorpusTests PRIVATE ../Fuzz)
target_compile_definitions(CowTermFuzzCorpusTests PRIVATE
        COWTERM_FUZZ_CORPUS_DIR="${PROJECT_SOURCE_DIR}/Fuzz/Corpus"
        COWTERM_FUZZ_SNAPSHOTS_FILE="${PROJECT_SOURCE_DIR}/Fuzz/Snapshots.txt")

# Renders a real pane off-screen and reads the pixels back, so it needs the GPU
# terminal rather than the model alone. Every case self-skips without a device,
# the way eacp's own GPU suites do.
nano_add_executable(CowTermRenderTests
        SOURCES
            RenderBackgroundTests.cpp
        TARGETS CowTerm::TerminalView)

if (WIN32)
    target_sources(CowTermRenderTests PRIVATE RenderBackgroundTests-Windows.cpp)
else ()
    target_sources(CowTermRenderTests PRIVATE RenderBackgroundTests-Posix.cpp)
endif ()

# The split tree, with real (in-process) panes. SessionView lives in the app
# rather than a library, so its sources compile straight in; the test supplies
# its own terminalConfig() so no pane ever talks to the session daemon.
nano_add_executable(CowTermSessionViewTests
        SOURCES
            SessionViewTests.cpp
            ../Terminal/SessionView.cpp
        TARGETS CowTerm::TerminalView CowTerm::Multiplexer eacp-ui SessionLog PondApp)

target_include_directories(CowTermSessionViewTests PRIVATE ../Terminal)

# Session switching against the process footprint, which only macOS reports
# this way (the GPU memory of a hidden session's panes).
if (APPLE)
    target_sources(CowTermSessionViewTests PRIVATE SessionMemoryTests-macOS.cpp)
endif ()

# The context list CowTerm publishes to Rudder and the branch it reads from
# each pane's repo; pure, so only the protocol types come along.
nano_add_executable(CowTermRudderTests
        SOURCES
            RudderContextsTests.cpp
            ../Terminal/RudderContexts.cpp
            ../Terminal/GitBranch.cpp
        TARGETS RudderProtocol)

target_include_directories(CowTermRudderTests PRIVATE ../Terminal)

# The `ct notify-user` request and reply encoding; pure, over Protocol.h.
nano_add_executable(CowTermNotifyUserTests
        SOURCES
            NotifyUserProtocolTests.cpp
        TARGETS CowTerm::Multiplexer)

# The overlay panels' shared list layout and key mapping, and the text
# splitting, wrapping and editing they and `ct notify-user` use.
nano_add_executable(CowTermPanelTests
        SOURCES
            OverlayPanelTests.cpp
            SearchFieldTests.cpp
            TextLayoutTests.cpp
            ../Terminal/OverlayPanel.cpp
            ../Terminal/SearchField.cpp
        TARGETS CowTerm::TerminalView CowTerm::Multiplexer eacp-ui emberstore FrecencySearch SearchKeys SessionLog PondApp)

target_include_directories(CowTermPanelTests PRIVATE ../Terminal)

# The phone's pane screen layout and bar gestures, and the overview's
# arrangement and bottom-anchored list, from iOS/, on the Mac. The phone's
# library is built only where libssh2 is (Apple), so Linux and Windows skip them.
if (TARGET CowTerm::Phone)
nano_add_executable(CowTermPaneScreenTests
        SOURCES
            PaneScreenLayoutTests.cpp
            TypingTests.cpp
            ClaudePromptTests.cpp
            OverviewLayoutTests.cpp
            OverviewScreenTests.cpp
            ../iOS/PaneScreenLayout.cpp
            ../iOS/Typing.cpp
            ../iOS/ClaudePrompt.cpp
            ../iOS/OverviewLayout.cpp
            ../iOS/OverviewScreen.cpp
        TARGETS eacp-graphics eacp-ui CowTerm::Phone CowTerm::TerminalView)

target_include_directories(CowTermPaneScreenTests PRIVATE ../iOS)
endif ()

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (LINUX)
    target_link_libraries(CowTermTests PRIVATE util)
endif ()

# The terminal widget's input and drawing: through its public surface, the
# bytes each key and mouse event writes to a recording shell and the cells copy
# mode and selection paint (pixel cases self-skip without a GPU device); and the
# pure encoders, copy-mode rules and painter decisions underneath.
nano_add_executable(CowTermViewTests
        SOURCES
            TerminalViewBehaviourTests.cpp
            TerminalViewLogicTests.cpp
            KittyKeyEncodingTests.cpp
            OutputParserThreadTests.cpp
            GraphemeTests.cpp
            RowDrawingCacheTests.cpp
            ScrollPositionTests.cpp
        TARGETS CowTerm::TerminalView)

# Guards the -fno-ipa-bit-cp in the top CMakeLists.txt: under GCC 15.3 with
# link-time optimisation, the row count reaches the receiver as 1 without it.
if (NOT MSVC)
    nano_add_executable(CowTermBuildTests
            SOURCES
                ClampedRowCountTests.cpp
                ClampedRowCount.cpp)

    # Link-time optimised like CowTerm's libraries, which is what the bug needs.
    set_default_target_setting(CowTermBuildTests)
endif ()

# The leader-key letters and digits AppShell turns into pane and session moves.
nano_add_executable(CowTermLeaderKeysTests
        SOURCES
            LeaderKeysTests.cpp
            ../Terminal/LeaderKeys.cpp)

target_include_directories(CowTermLeaderKeysTests PRIVATE ../Terminal)

# `ct`'s argument parsing and its recording state file; pure, no OS calls.
nano_add_executable(CowTermCliTests
        SOURCES
            CliParsingTests.cpp
            PaneStateArgumentsTests.cpp
        TARGETS CowTerm::Multiplexer)

# The palette's order: match level, learned picks, usage.
nano_add_executable(CowTermPaletteRankingTests
        SOURCES
            PaletteRankingTests.cpp
        TARGETS CowTerm::Multiplexer)

# Not a test: `cat` of a large file through a real pane, timing the output and
# the UI thread's responsiveness meanwhile. Run it by hand before and after a
# change to the output path.
add_executable(CowTermCatThroughputBench ../Bench/CatThroughputBench.cpp)
target_link_libraries(CowTermCatThroughputBench PRIVATE CowTerm::TerminalView)

# Not a test: one pane's memory with 10,000 lines of scrollback.
add_executable(CowTermScreenMemoryBench ../Bench/ScreenMemoryBench.cpp)
target_link_libraries(CowTermScreenMemoryBench PRIVATE CowTerm::Emulator)
# Drives the real session daemon over IPC under its own instance name: a pane's
# screen must come back after the daemon quits and starts again.
nano_add_executable(CowTermDaemonRestoreTests
        SOURCES
            DaemonRestoreTests.cpp
        TARGETS CowTerm::Multiplexer eacp-network PondApp)

target_compile_definitions(CowTermDaemonRestoreTests
        PRIVATE COWTERM_DAEMON_PATH="$<TARGET_FILE:CowTermDaemon>")
add_dependencies(CowTermDaemonRestoreTests CowTermDaemon)

# The daemon's side of the phone: size owners, the pane last typed into,
# local sessions and clients a protocol version apart, over IPC as above.
nano_add_executable(CowTermDaemonPhoneProtocolTests
        SOURCES
            DaemonPhoneProtocolTests.cpp
        TARGETS CowTerm::Multiplexer eacp-network PondApp)

target_compile_definitions(CowTermDaemonPhoneProtocolTests
        PRIVATE COWTERM_DAEMON_PATH="$<TARGET_FILE:CowTermDaemon>")
add_dependencies(CowTermDaemonPhoneProtocolTests CowTermDaemon)

# Who sets a pane's size when the Mac and the phone both show it.
nano_add_executable(CowTermPaneSizeOwnerTests
        SOURCES
            PaneSizeOwnerTests.cpp
            DaemonClientSizeOwnerTests.cpp
            SessionOverviewTests.cpp
            SessionOverviewSearchTests.cpp
        TARGETS CowTerm::Multiplexer)

# Remote sessions: searchDirs host entries and the framing `ct bridge` speaks
# over ssh.
nano_add_executable(CowTermRemoteTests
        SOURCES
            RemoteLocationTests.cpp
            BridgeFramingTests.cpp
            BridgeRelayTests.cpp
        TARGETS CowTerm::Multiplexer)

# Host colours, what an ssh or ct bridge failure means for the user, the
# bridge's hello and framing under every link, and the seam RemoteHost makes
# its link through.
nano_add_executable(CowTermRemoteAppTests
        SOURCES
            HostColoursTests.cpp
            ColourBlindHostColoursTests.cpp
            SshBridgeLinkTests.cpp
            BridgeLinkTests.cpp
            RemoteHostLinkFactoryTests.cpp
        TARGETS CowTerm::Multiplexer)

# Not a test: Ctrl+A F rendered off screen to a PNG with the user's config
# (scripts/render-picker.sh), to look at the picker without opening a window.
add_executable(CowTermRenderPicker
        ../Bench/RenderPicker.cpp
        ../Terminal/Command.cpp
        ../Terminal/PaneTerminalConfig.cpp
        ../Terminal/OverlayPanel.cpp
        ../Terminal/Palette.cpp
        ../Terminal/PaletteUsageStore.cpp
        ../Terminal/SearchField.cpp
        ../Terminal/Session.cpp
        ../Terminal/SessionView.cpp)
if (WIN32)
    target_sources(CowTermRenderPicker PRIVATE ../Terminal/Command-Windows.cpp)
    target_compile_options(CowTermRenderPicker PRIVATE /utf-8)
else ()
    target_sources(CowTermRenderPicker PRIVATE ../Terminal/Command-Posix.cpp)
endif ()
target_include_directories(CowTermRenderPicker PRIVATE ../Terminal)
target_link_libraries(CowTermRenderPicker PRIVATE
        CowTerm::TerminalView CowTerm::Multiplexer eacp-ui eacp-text emberstore FrecencySearch SearchKeys SessionLog PondApp)

# Not a test: the palette, the switcher and a session's panes rendered off
# screen to PNGs with sessions on Jamie's hosts and scripted shells
# (scripts/render-host-colours.sh), to look at the host colours.
add_executable(CowTermRenderHostColours
        ../Bench/RenderHostColours.cpp
        ../Terminal/Command.cpp
        ../Terminal/OverlayPanel.cpp
        ../Terminal/Palette.cpp
        ../Terminal/PaletteUsageStore.cpp
        ../Terminal/SearchField.cpp
        ../Terminal/Session.cpp
        ../Terminal/SessionView.cpp
        ../Terminal/Switcher.cpp)
if (WIN32)
    target_sources(CowTermRenderHostColours PRIVATE ../Terminal/Command-Windows.cpp)
    target_compile_options(CowTermRenderHostColours PRIVATE /utf-8)
else ()
    target_sources(CowTermRenderHostColours PRIVATE ../Terminal/Command-Posix.cpp)
endif ()
target_include_directories(CowTermRenderHostColours PRIVATE ../Terminal)
target_link_libraries(CowTermRenderHostColours PRIVATE
        CowTerm::TerminalView CowTerm::Multiplexer eacp-ui eacp-text emberstore FrecencySearch SearchKeys SessionLog PondApp)

# Not a test: Ctrl+A i's popup for a pane on a remote host, opened with no
# window so its trace shows what the command drew (scripts/run-remote-popup.sh).
add_executable(CowTermRunRemotePopup
        ../Bench/RunRemotePopup.cpp
        ../Terminal/PaneTerminalConfig.cpp
        ../Terminal/Popup.cpp)
target_include_directories(CowTermRunRemotePopup PRIVATE ../Terminal)
target_link_libraries(CowTermRunRemotePopup PRIVATE CowTerm::TerminalView CowTerm::Multiplexer SessionLog PondApp CowTermVersion)

# Not a test: an in-process ssh link (EmbeddedSshLink) to a host's daemon,
# printing a throwaway pane's screen (scripts/probe-embedded-ssh.sh). Where
# libssh2 is built, with the parsing of its command line tested beside it.
if (TARGET CowTerm::EmbeddedSsh)
    add_executable(CowTermEmbeddedSshProbe
            ../Bench/EmbeddedSshProbe.cpp
            ../Bench/EmbeddedSshProbeArguments.cpp)
    target_link_libraries(CowTermEmbeddedSshProbe PRIVATE CowTerm::EmbeddedSsh CowTerm::TerminalView)

    nano_add_executable(CowTermEmbeddedSshTests
            SOURCES
                EmbeddedSshTests.cpp
                ../Bench/EmbeddedSshProbeArguments.cpp
            TARGETS CowTerm::EmbeddedSsh)
    target_include_directories(CowTermEmbeddedSshTests PRIVATE ../Bench)

    # The phone's connection layer without a phone or a host: settings, pane
    # choice, link stalls, the stored last screen, the mirrored pane, and
    # PhoneConnection over a recorded link (it never spawns an unlisted pane).
    nano_add_executable(CowTermPhoneTests
            SOURCES
                PhoneTests.cpp
                PhoneConnectionTests.cpp
                PhoneOverviewTests.cpp
            TARGETS CowTerm::Phone)
endif ()

# The headless ct a remote host runs, and the app's own ct mode.
pond_add_version_test(CowTermCli ct)
get_directory_property(appExecutableTarget
        DIRECTORY ${PROJECT_SOURCE_DIR}/Terminal DEFINITION COWTERM_EXECUTABLE_TARGET)
pond_add_version_test(${appExecutableTarget} ct)
