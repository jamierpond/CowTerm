# Unit tests. NanoTest is a header-plus-tiny-lib framework that generates a
# main() and registers each test case with CTest (see nano_add_executable).
CPMAddPackage(
        NAME NanoTest
        GITHUB_REPOSITORY eyalamirmusic/NanoTest
        GIT_TAG v1.0.0)

# The PTY teardown lives in Pty-Posix.cpp, which has no eacp dependencies, so we
# compile it straight into the test binary rather than through the app targets.
nano_add_executable(CowTermTests
        SOURCES
            PtyTests.cpp
            ../Terminal/Pty-Posix.cpp)

target_include_directories(CowTermTests PRIVATE ../Terminal)

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (NOT APPLE)
    target_link_libraries(CowTermTests PRIVATE util)
endif ()
