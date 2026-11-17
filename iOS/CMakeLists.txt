# CowTerm for iPhone (docs/iOSDesign.md). For now one TerminalView filling the
# screen: a pane on a host when the launch names one (PhoneLaunch), fixed
# sample text otherwise.
eacp_add_app(CowTermMobile Main.cpp InterfaceOrientation-iOS.mm
        PhoneLaunch.cpp NetworkPathWatch-iOS.mm
        BUNDLE_ID audio.pond.cowterm.mobile
        DISPLAY_NAME "CowTerm")

target_link_libraries(CowTermMobile PRIVATE CowTerm::TerminalView CowTerm::Phone "-framework Network")
set_target_properties(CowTermMobile PROPERTIES MACOSX_BUNDLE TRUE)

# The Mac app's JetBrains Mono, embedded the same way.
res_embed_add(CowTermMobile DIRECTORY ${PROJECT_SOURCE_DIR}/Terminal/Resources)

eacp_set_gui_subsystem(CowTermMobile)
set_default_target_setting(CowTermMobile)
