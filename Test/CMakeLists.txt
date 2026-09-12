# Unit tests. NanoTest is a header-plus-tiny-lib framework that generates a
# main() and registers each test case with CTest (see nano_add_executable).
CPMAddPackage(
        NAME NanoTest
        GITHUB_REPOSITORY eyalamirmusic/NanoTest
        GIT_TAG main
)

# The PTY teardown lives in Pty-Posix.cpp, which has no eacp dependencies, so we
# compile it straight into the test binary rather than through the library
# target.
nano_add_executable(CowTermTests
        SOURCES
            PtyTests.cpp
            ../Lib/CowTermCore/Pty-Posix.cpp)

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

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (NOT APPLE)
    target_link_libraries(CowTermTests PRIVATE util)
endif ()
