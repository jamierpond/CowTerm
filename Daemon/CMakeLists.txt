# The session daemon: a headless PTY server the GUI dials over eacp IPC.
# The PTY comes from CowTerm::Core; Protocol.h stays shared source with the
# app — the wire format is the product's, not the library's.
add_executable(CowTermDaemon Main.cpp)

target_include_directories(CowTermDaemon PRIVATE ../Terminal)
target_link_libraries(CowTermDaemon PRIVATE CowTerm::Core eacp-network)

set_default_target_setting(CowTermDaemon)
