# Sessions across processes and machines: the session daemon's protocol and
# the app's client of it, the ssh link and `ct bridge` framing to a remote
# daemon, remote hosts and their projects, and the config, data directories
# and `ct` plumbing that the daemon, `ct` and the app all share. No GPU: the
# headless build (ct and cowtermd for a remote machine) links this.
file(READ ${PROJECT_SOURCE_DIR}/scripts/remote-bootstrap.sh REMOTE_BOOTSTRAP_SCRIPT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/scripts/remote-bootstrap.sh)
configure_file(RemoteBootstrapScript.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/RemoteBootstrapScript.h @ONLY)

add_library(CowTermMultiplexer STATIC
        Bridge.cpp
        BridgeFraming.cpp
        BridgeLink.cpp
        BridgeRelay.cpp
        CliParsing.cpp
        Config.cpp
        DaemonClient.cpp
        DaemonDialer.cpp
        DataDirectories.cpp
        HostColours.cpp
        NotifyUserClient.cpp
        NotifyUserProtocol.cpp
        ProjectScan.cpp
        Projects.cpp
        RemoteCommands.cpp
        RemoteHosts.cpp
        RemoteInstall.cpp
        RemoteLocation.cpp
        SshBridgeLink.cpp
        TextLayout.cpp
        UsageLog.cpp)

if (WIN32)
    target_sources(CowTermMultiplexer PRIVATE StandardStreams-Windows.cpp)
    target_compile_options(CowTermMultiplexer PRIVATE /utf-8)
else ()
    target_sources(CowTermMultiplexer PRIVATE StandardStreams-Posix.cpp)
endif ()

# emberstore watches files with FSEvents, which iOS does not have, so it does
# not build there. Only the Mac app's palette uses MruStore.
if (NOT IOS)
    target_sources(CowTermMultiplexer PRIVATE MruStore.cpp)
    target_link_libraries(CowTermMultiplexer PUBLIC emberstore)
endif ()

target_include_directories(CowTermMultiplexer
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(CowTermMultiplexer PUBLIC
        CowTerm::Emulator
        CowTerm::Shell
        eacp-network
        PondApp
        SessionLog)

add_library(CowTerm::Multiplexer ALIAS CowTermMultiplexer)

set_default_target_setting(CowTermMultiplexer)
