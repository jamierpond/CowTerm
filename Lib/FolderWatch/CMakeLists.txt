# RecursiveFolderWatcher: a folder and everything under it, watched with the
# platform's own change events. Depends on eacp's core only, so it can move
# into eacp (Core/Utils) with its tests; the daemon uses it to see project
# folders change. Not on iOS, which runs no daemon.
add_library(CowTermFolderWatch STATIC RecursiveFolderWatcher.cpp)

if (APPLE)
    target_sources(CowTermFolderWatch PRIVATE RecursiveFolderWatcher-macOS.mm)
    target_link_libraries(CowTermFolderWatch PRIVATE "-framework CoreServices")
elseif (WIN32)
    target_sources(CowTermFolderWatch PRIVATE RecursiveFolderWatcher-Windows.cpp)
else ()
    target_sources(CowTermFolderWatch PRIVATE RecursiveFolderWatcher-Linux.cpp)
endif ()

target_include_directories(CowTermFolderWatch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(CowTermFolderWatch PUBLIC eacp-core)

add_library(CowTerm::FolderWatch ALIAS CowTermFolderWatch)

set_default_target_setting(CowTermFolderWatch)
