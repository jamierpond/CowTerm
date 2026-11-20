# `ct push-relay`: holds the phones' device tokens and Apple's push key, signs
# each request with ES256 (mbedTLS) and sends it to Apple's push service.
add_library(CowTermPushRelay STATIC
        ApnsDelivery.cpp
        ApnsProviderToken.cpp
        PushRelay.cpp
        PushRelayCommand.cpp
        PushRelayServer.cpp)

if (WIN32)
    target_sources(CowTermPushRelay PRIVATE LocalTime-Windows.cpp)
else ()
    target_sources(CowTermPushRelay PRIVATE LocalTime-Posix.cpp)
endif ()

target_include_directories(CowTermPushRelay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermPushRelay PUBLIC CowTerm::Push eacp-network PRIVATE mbedcrypto)

add_library(CowTerm::PushRelay ALIAS CowTermPushRelay)

set_default_target_setting(CowTermPushRelay)
