# CowTerm for iPhone (docs/iOSDesign.md): the pane screen, showing a pane on a
# host chosen at launch or from the overview (PhoneLaunch), or with --sample
# an invented pane and overview for screenshots.
eacp_add_app(CowTermMobile
        Main.cpp
        InterfaceOrientation-iOS.mm
        NetworkPathWatch-iOS.mm
        OverviewLayout.cpp
        OverviewSamples.cpp
        OverviewScreen.cpp
        PaneScreen.cpp
        PaneScreenChrome.cpp
        PaneScreenLayout.cpp
        PhoneLaunch.cpp PhoneOverviewBinding.cpp
        PhonePaneBinding.cpp
        PhoneDiffViewBinding.cpp
        DiffViewScreen.cpp
        NoPaneYetShell.cpp
        SampleShell.cpp
        StoredScreenShell.cpp
        Typing.cpp
        TypingControls.cpp
        ClaudePrompt.cpp
        OutputWatchingShell.cpp
        ScreenAwake-iOS.mm
        ${PROJECT_SOURCE_DIR}/Terminal/NotifyUserPrompt.cpp
        ${PROJECT_SOURCE_DIR}/Terminal/PasteConfirmation.cpp
        BUNDLE_ID audio.pond.cowterm.mobile
        DISPLAY_NAME "CowTerm")

target_link_libraries(CowTermMobile PRIVATE
        CowTerm::TerminalView CowTerm::Multiplexer CowTerm::Phone eacp-ui "-framework Network")
# The Mac's paste confirmation, reused as it is.
target_include_directories(CowTermMobile PRIVATE ${PROJECT_SOURCE_DIR}/Terminal)
# The cow icon in every iPhone size, from scripts/make-ios-app-icon.sh.
target_sources(CowTermMobile PRIVATE Resources/Assets.xcassets)
set_source_files_properties(Resources/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

set_target_properties(CowTermMobile PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon)

# The Mac app's JetBrains Mono, embedded the same way.
res_embed_add(CowTermMobile DIRECTORY ${PROJECT_SOURCE_DIR}/Terminal/Resources)

eacp_set_gui_subsystem(CowTermMobile)
set_default_target_setting(CowTermMobile)
