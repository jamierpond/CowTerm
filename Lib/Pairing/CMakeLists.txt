# The phone pairing itself (docs/iOSApp.md, "Pairing"): its key's
# authorized_keys line and signatures in ssh's encoding, the computers it was
# given, and trusting a host key on first connection. No UI and nothing
# iOS-only, so it is tested on the Mac.
add_library(CowTermPairing STATIC
        HostPairing.cpp
        HostTrust.cpp
        SshKeyEncoding.cpp)

target_include_directories(CowTermPairing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermPairing PUBLIC CowTerm::EmbeddedSsh)

add_library(CowTerm::Pairing ALIAS CowTermPairing)

set_default_target_setting(CowTermPairing)
