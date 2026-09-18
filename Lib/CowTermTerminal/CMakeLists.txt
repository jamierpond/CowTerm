# The embeddable terminal: one GPU-rendered pane over a live shell, and
# nothing around it. This is the target another project links; sessions,
# splits and the daemon stay in the app. The widget's TerminalConfig carries
# the presentation and an injectable shell factory, so a consumer needs no
# CowTerm config file and no session daemon.
add_library(CowTermTerminal STATIC
        GlyphAtlas.cpp
        TerminalView.cpp)

if (MSVC)
    # Unicode literals (copy-mode indicators) — same reason as CowTermCore.
    target_compile_options(CowTermTerminal PRIVATE /utf-8)
endif ()

# ResEmbed for registerEmbeddedFonts(): the runtime lookup lives here, the
# resources themselves are embedded by the executable (res_embed_add). With
# no fonts embedded the call is a no-op and name lookup falls back to the
# platform font system.
find_package(ResEmbed REQUIRED)

target_include_directories(CowTermTerminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermTerminal PUBLIC
        CowTermCore
        eacp-sprites
        eacp-text
        ResEmbed)

add_library(CowTerm::Terminal ALIAS CowTermTerminal)

set_default_target_setting(CowTermTerminal)
