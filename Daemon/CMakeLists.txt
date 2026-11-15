# The session daemon: a headless PTY server the GUI dials over eacp IPC.
# The PTY comes from CowTerm::Shell; the wire format (Protocol.h) and the data
# directories from CowTerm::Multiplexer, shared with the app and ct.
add_executable(CowTermDaemon Main.cpp)

if (WIN32)
    target_sources(CowTermDaemon PRIVATE CommandLookup-Windows.cpp Signals-Windows.cpp)
else ()
    target_sources(CowTermDaemon PRIVATE CommandLookup-Posix.cpp Signals-Posix.cpp)
endif ()

target_link_libraries(CowTermDaemon PRIVATE
        CowTerm::Multiplexer CowTerm::Emulator CowTerm::Shell eacp-network emberstore PondApp SessionLog)

add_dependencies(CowTermDaemon CowTermVersion)
target_include_directories(CowTermDaemon PRIVATE ${PROJECT_BINARY_DIR}/generated)

set_default_target_setting(CowTermDaemon)

if (APPLE OR WIN32)
    eacp_enable_crash_reporting(CowTermDaemon SYMBOLS_DIR ${PROJECT_BINARY_DIR}/symbols)
endif ()
