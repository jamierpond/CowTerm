# The process behind a pane: the PTY (forkpty or ConPTY) and the Shell
# interface a pane talks to, with LocalShell running it in-process.
add_library(CowTermShell STATIC)

if (WIN32)
    target_sources(CowTermShell PRIVATE Pty-Windows.cpp)
    target_compile_options(CowTermShell PRIVATE /utf-8)
else ()
    target_sources(CowTermShell PRIVATE Pty-Posix.cpp)
endif ()

# forkpty/openpty live in libutil on Linux; on Apple they are in libSystem.
if (UNIX AND NOT APPLE)
    target_link_libraries(CowTermShell PRIVATE util)
endif ()

target_include_directories(CowTermShell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermShell PUBLIC eacp-core)

add_library(CowTerm::Shell ALIAS CowTermShell)

set_default_target_setting(CowTermShell)
