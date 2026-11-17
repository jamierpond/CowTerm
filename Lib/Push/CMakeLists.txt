# What a daemon sends the push relay when a Claude finishes or asks, whether
# it should, and the notification Apple delivers for it. The daemon, the relay
# and the phone share it; no crypto and no server here.
add_library(CowTermPush STATIC
        ApnsPayload.cpp
        PanePushGate.cpp
        PushNotice.cpp)

target_include_directories(CowTermPush PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermPush PUBLIC eacp-core)

add_library(CowTerm::Push ALIAS CowTermPush)

set_default_target_setting(CowTermPush)
