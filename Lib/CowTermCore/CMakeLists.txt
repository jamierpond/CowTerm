# The emulator model: escape-sequence parser, screen/scrollback, and the PTY
# with the Shell seam over it. No GPU, no daemon, no config file — the daemon
# and the PTY tests link this without dragging in a renderer.
add_library(CowTermCore STATIC
        TermScreen.cpp
        TermParser.cpp)

if (WIN32)
    target_sources(CowTermCore PRIVATE Pty-Windows.cpp)

    # The parser and screen sources hold Unicode literals (box drawing,
    # powerline glyphs); MSVC assumes the ANSI codepage without this.
    target_compile_options(CowTermCore PRIVATE /utf-8)
else ()
    target_sources(CowTermCore PRIVATE Pty-Posix.cpp)
endif ()

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (UNIX AND NOT APPLE)
    target_link_libraries(CowTermCore PRIVATE util)
endif ()

target_include_directories(CowTermCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermCore PUBLIC eacp-core)

add_library(CowTerm::Core ALIAS CowTermCore)

set_default_target_setting(CowTermCore)
