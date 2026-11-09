# Unit tests. NanoTest is a header-plus-tiny-lib framework that generates a
# main() and registers each test case with CTest (see nano_add_executable).
CPMAddPackage(
        NAME NanoTest
        GITHUB_REPOSITORY eyalamirmusic/NanoTest
        GIT_TAG main
)

# The PTY teardown, with each platform's own cases beside the shared ones.
# Pty-Posix.cpp has no eacp dependencies, so it compiles straight into the test
# binary rather than through the library target; ConPTY's Pty-Windows.cpp leans
# on eacp's string helpers, so there the binary links the library instead.
if (WIN32)
    nano_add_executable(CowTermTests
            SOURCES
                PtyTests.cpp
                PtyTests-Windows.cpp
            TARGETS CowTerm::Core)
else ()
    nano_add_executable(CowTermTests
            SOURCES
                PtyTests.cpp
                PtyTests-Posix.cpp
                ../Lib/CowTermCore/Pty-Posix.cpp)
endif ()

target_include_directories(CowTermTests PRIVATE ../Lib/CowTermCore)

# The parser and screen do pull in eacp (Rgb and friends), so these link the
# library rather than compiling its sources in the way the PTY tests do.
nano_add_executable(CowTermScreenTests
        SOURCES
            ScreenColorTests.cpp
            ParserFuzzRegressionTests.cpp
            TerminalModeTests.cpp
            ReflowTests.cpp
        TARGETS CowTerm::Core)

# Replays every recording in Fuzz/Corpus (the fuzzer's seeds and every input
# it once crashed on) and checks each screen against Fuzz/Snapshots.txt.
nano_add_executable(CowTermFuzzCorpusTests
        SOURCES
            FuzzCorpusReplayTests.cpp
        TARGETS CowTerm::Core)

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
        TARGETS CowTerm::Terminal)

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
            ../Terminal/Config.cpp
        TARGETS CowTerm::Terminal eacp-ui)

target_include_directories(CowTermSessionViewTests PRIVATE ../Terminal)

# The privacy-grants model behind the permissions panel: pure, so it builds
# everywhere even though only macOS ships the panel.
nano_add_executable(CowTermPermissionsTests
        SOURCES
            PermissionsTests.cpp
            ../Terminal/Permissions.cpp)

target_include_directories(CowTermPermissionsTests PRIVATE ../Terminal)

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
            ../Terminal/NotifyUserProtocol.cpp
            ../Terminal/TextLayout.cpp)

target_include_directories(CowTermNotifyUserTests PRIVATE ../Terminal)

# The overlay panels' shared list layout and key mapping, and the text
# splitting, wrapping and editing they and `ct notify-user` use.
nano_add_executable(CowTermPanelTests
        SOURCES
            OverlayPanelTests.cpp
            TextLayoutTests.cpp
            ../Terminal/OverlayPanel.cpp
            ../Terminal/TextLayout.cpp
            ../Terminal/Config.cpp
        TARGETS CowTerm::Terminal eacp-ui emberstore)

target_include_directories(CowTermPanelTests PRIVATE ../Terminal)

# The cached Visual Studio developer environment; pure, so it builds everywhere
# though only Windows panes use it.
nano_add_executable(CowTermDevShellCacheTests
        SOURCES
            DevShellEnvironmentCacheTests.cpp
        TARGETS CowTerm::Core)

target_include_directories(CowTermDevShellCacheTests PRIVATE ../Lib/CowTermCore)

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
        TARGETS CowTerm::Terminal)

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
            ../Terminal/CliParsing.cpp)

target_include_directories(CowTermCliTests PRIVATE ../Terminal)

# The palette's fzf-style scorer.
nano_add_executable(CowTermFuzzyMatchTests
        SOURCES
            FuzzyMatchTests.cpp
            ../Terminal/FuzzyMatch.cpp)

target_include_directories(CowTermFuzzyMatchTests PRIVATE ../Terminal)

# Not a test: `cat` of a large file through a real pane, timing the output and
# the UI thread's responsiveness meanwhile. Run it by hand before and after a
# change to the output path.
add_executable(CowTermCatThroughputBench ../Bench/CatThroughputBench.cpp)
target_link_libraries(CowTermCatThroughputBench PRIVATE CowTerm::Terminal)

# Not a test: one pane's memory with 10,000 lines of scrollback.
add_executable(CowTermScreenMemoryBench ../Bench/ScreenMemoryBench.cpp)
target_link_libraries(CowTermScreenMemoryBench PRIVATE CowTerm::Core)

# Not a test: times a new pane's PowerShell to its first prompt, for
# Scripts/measure-shell-start.ps1.
if (WIN32)
    add_executable(CowTermShellStartTimer ShellStartTimer-Windows.cpp)
    target_link_libraries(CowTermShellStartTimer PRIVATE CowTerm::Core)
    target_include_directories(CowTermShellStartTimer PRIVATE ../Lib/CowTermCore)
endif ()
