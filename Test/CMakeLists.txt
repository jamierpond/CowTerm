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
        TARGETS CowTerm::Core)

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

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (LINUX)
    target_link_libraries(CowTermTests PRIVATE util)
endif ()
