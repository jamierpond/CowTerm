# What CowTerm needs on another machine: ssh carries the session daemon's
# messages through `ct bridge`. CowTermRemote is shared with the app (which
# dials remotes); CowTermCli is the headless ct a remote installs.
file(READ ${PROJECT_SOURCE_DIR}/scripts/remote-bootstrap.sh REMOTE_BOOTSTRAP_SCRIPT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/scripts/remote-bootstrap.sh)
configure_file(RemoteBootstrapScript.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/RemoteBootstrapScript.h @ONLY)

add_library(CowTermRemote STATIC
        Bridge.cpp
        BridgeFraming.cpp
        BridgeRelay.cpp
        DaemonDialer.cpp
        ProjectScan.cpp
        RemoteCommands.cpp
        RemoteInstall.cpp
        RemoteLocation.cpp)

if (WIN32)
    target_sources(CowTermRemote PRIVATE StandardStreams-Windows.cpp)
else ()
    target_sources(CowTermRemote PRIVATE StandardStreams-Posix.cpp)
endif ()

target_include_directories(CowTermRemote
        PUBLIC . ../Terminal
        PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(CowTermRemote PUBLIC eacp-network)
set_default_target_setting(CowTermRemote)

add_executable(CowTermCli
        CtMain.cpp
        ../Terminal/CliParsing.cpp
        ../Terminal/Config.cpp
        ../Terminal/NotifyUserClient.cpp
        ../Terminal/NotifyUserProtocol.cpp)
target_link_libraries(CowTermCli PRIVATE CowTermRemote CowTerm::Core)
add_dependencies(CowTermCli CowTermVersion)
target_include_directories(CowTermCli PRIVATE ${PROJECT_BINARY_DIR}/generated)
set_target_properties(CowTermCli PROPERTIES OUTPUT_NAME ct)
set_default_target_setting(CowTermCli)
