# The phone's side of a daemon (docs/iOSDesign.md): which pane to open, how
# the link is doing, the stored last screen and redialling on wake, over
# EmbeddedSshLink. No UI and nothing iOS-only, so it builds and is tested on
# the Mac too. Where libssh2 is built.
add_library(CowTermPhone STATIC
        DiffViewPane.cpp
        HeldPane.cpp
        LinkWatch.cpp
        MirroredShell.cpp
        PaneChoice.cpp
        PhoneConnection.cpp
        PhoneOverview.cpp
        PhoneSettings.cpp
        ProjectSession.cpp
        PushTarget.cpp
        SessionOpenings.cpp
        StoredScreen.cpp)

target_include_directories(CowTermPhone PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermPhone PUBLIC CowTerm::EmbeddedSsh CowTerm::Multiplexer CowTerm::Push)

add_library(CowTerm::Phone ALIAS CowTermPhone)

set_default_target_setting(CowTermPhone)
