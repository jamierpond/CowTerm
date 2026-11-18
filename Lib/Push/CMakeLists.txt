# What a daemon sends the push relay for a pane's new unread event, and the
# notification Apple delivers for it. The daemon, the relay and the phone
# share it; no crypto and no server here.
add_library(CowTermPush STATIC
        ApnsPayload.cpp
        PushNotice.cpp)

target_include_directories(CowTermPush PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermPush PUBLIC CowTerm::Multiplexer)

add_library(CowTerm::Push ALIAS CowTermPush)

set_default_target_setting(CowTermPush)
