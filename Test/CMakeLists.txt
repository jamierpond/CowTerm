# Unit tests. NanoTest is a header-plus-tiny-lib framework that generates a
# main() and registers each test case with CTest (see nano_add_executable).
CPMAddPackage(
        NAME NanoTest
        GITHUB_REPOSITORY eyalamirmusic/NanoTest
        GIT_TAG main
)

# The PTY teardown lives in Pty-Posix.cpp, which has no eacp dependencies, so we
# compile it straight into the test binary rather than through the app targets.
nano_add_executable(CowTermTests
        SOURCES
            PtyTests.cpp
            ../Terminal/Pty-Posix.cpp)

target_include_directories(CowTermTests PRIVATE ../Terminal)

# The web gateway's transport layer (websocket framing, attach snapshots),
# exercised over real loopback sockets — no GUI, so it lives here rather
# than behind the app target.
nano_add_executable(CowTermWebTests
        SOURCES
            WebTests.cpp
            ../Terminal/Web/WebSocket.cpp
            ../Terminal/Web/ScreenSerializer.cpp
            ../Terminal/TermScreen.cpp
            ../Terminal/TermParser.cpp)

target_include_directories(CowTermWebTests PRIVATE ../Terminal)
target_link_libraries(CowTermWebTests PRIVATE eacp-network)

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (NOT APPLE)
    target_link_libraries(CowTermTests PRIVATE util)
endif ()
