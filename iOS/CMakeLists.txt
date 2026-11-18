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
        PhonePushNotifications.cpp
        PhonePaneBinding.cpp
        PhoneDiffViewBinding.cpp
        DiffViewScreen.cpp
        DiffViewLaunchSteps.cpp
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

# Push notifications need a paid team's signing profile; Jamie's free Personal
# Team cannot sign the entitlement, so it is off unless asked for. Xcode signs
# a TestFlight build's "development" as production on its own.
option(COWTERM_IOS_PUSH_NOTIFICATIONS "Sign the iPhone app with the push notifications entitlement" OFF)
set(COWTERM_IOS_APNS_ENVIRONMENT "sandbox" CACHE STRING
        "Which Apple push service this build's device token belongs to: sandbox for a development build, production for TestFlight")

if (COWTERM_IOS_PUSH_NOTIFICATIONS)
    set_target_properties(CowTermMobile PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/CowTermMobile.entitlements")
endif ()

target_compile_definitions(CowTermMobile PRIVATE
        COWTERM_IOS_APNS_ENVIRONMENT="${COWTERM_IOS_APNS_ENVIRONMENT}")

# The phone registers its push token with the relay over plain HTTP on the
# tailnet, at an unqualified name ("macprorack"), which App Transport Security
# allows only with local networking on.
eacp_append_plist_xml(CowTermMobile
        "\t<key>NSAppTransportSecurity</key>\n\t<dict>\n\t\t<key>NSAllowsLocalNetworking</key>\n\t\t<true/>\n\t</dict>\n")

# The push's sound, made by scripts/make-ba-ding-sound.py.
set(push_sound Resources/ba-ding.caf)
target_sources(CowTermMobile PRIVATE ${push_sound})
set_source_files_properties(${push_sound} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# The Mac app's JetBrains Mono, embedded the same way.
res_embed_add(CowTermMobile DIRECTORY ${PROJECT_SOURCE_DIR}/Terminal/Resources)

eacp_set_gui_subsystem(CowTermMobile)
set_default_target_setting(CowTermMobile)
