# The session daemon: a headless PTY server the GUI dials over eacp IPC.
# The PTY comes from CowTerm::Shell; the wire format (Protocol.h) and the data
# directories from CowTerm::Multiplexer, shared with the app and ct.
add_executable(CowTermDaemon Main.cpp PanePushNotifier.cpp ProjectFileChanges.cpp UnreadPanes.cpp)

if (WIN32)
    target_sources(CowTermDaemon PRIVATE CommandLookup-Windows.cpp Signals-Windows.cpp)
else ()
    target_sources(CowTermDaemon PRIVATE CommandLookup-Posix.cpp Signals-Posix.cpp)
endif ()

target_link_libraries(CowTermDaemon PRIVATE
        CowTerm::Multiplexer CowTerm::FolderWatch CowTerm::Push CowTerm::Emulator CowTerm::Shell eacp-network emberstore PondApp SessionLog
        CowTermVersion)

set_default_target_setting(CowTermDaemon)
pond_write_line_coverage(CowTermDaemon APP_NAME CowTerm INSTANCE_VARIABLE COWTERM_INSTANCE)

# Crash reports in pond/CowTerm/Crashes/CowTermDaemon, which just crash-reports lists.
pond_enable_crash_reports(CowTermDaemon)
