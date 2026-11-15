# The emulator model: escape-sequence parser, screen/scrollback, reflow. Bytes
# in, screen state out: no process, no GPU, no daemon, no config file.
add_library(CowTermEmulator STATIC
        CharWidth.cpp
        GraphemeBreak.cpp
        CsiParameters.cpp
        FocusReports.cpp
        HyperlinkTable.cpp
        KittyKeyboardFlags.cpp
        Reflow.cpp
        SelectGraphicRendition.cpp
        TermScreen.cpp
        TermParser.cpp
        TermState.cpp
        TerminalModel.cpp)

if (WIN32)
    # The parser and screen sources hold Unicode literals (box drawing,
    # powerline glyphs); MSVC assumes the ANSI codepage without this.
    target_compile_options(CowTermEmulator PRIVATE /utf-8)
endif ()

target_include_directories(CowTermEmulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermEmulator PUBLIC eacp-core)

add_library(CowTerm::Emulator ALIAS CowTermEmulator)

set_default_target_setting(CowTermEmulator)
