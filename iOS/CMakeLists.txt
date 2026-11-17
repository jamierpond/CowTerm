# CowTerm for iPhone (docs/iOSDesign.md): the pane screen, showing a pane on a
# host chosen at launch or from the overview (PhoneLaunch), or with --sample
# an invented pane and overview for screenshots.
set(COWTERM_IOS_BUILD_NUMBER 1 CACHE STRING
        "CFBundleVersion: scripts/release-ios.sh sets it to the commit count, so each upload's is higher")

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
        DeviceKey-iOS.mm
        PairingLayout.cpp
        PairingScreen.cpp
        PhonePairing.cpp
        QrCode-iOS.mm
        OutputWatchingShell.cpp
        ScreenAwake-iOS.mm
        ${PROJECT_SOURCE_DIR}/Terminal/NotifyUserPrompt.cpp
        ${PROJECT_SOURCE_DIR}/Terminal/PasteConfirmation.cpp
        BUNDLE_ID audio.pond.cowterm.mobile
        DISPLAY_NAME "CowTerm"
        VERSION ${PROJECT_VERSION}
        VERSION_CODE ${COWTERM_IOS_BUILD_NUMBER})

# The app's ssh client (libssh2 on mbedTLS) encrypts with its own standard
# algorithms, not only the system's, so the export answer is yes.
eacp_add_plist_entries(CowTermMobile
        CFBundleDisplayName "CowTerm"
        LSRequiresIPhoneOS TRUE
        ITSAppUsesNonExemptEncryption TRUE
        NSLocalNetworkUsageDescription "CowTerm connects over ssh to the computers you add, which may be on your local network.")

target_sources(CowTermMobile PRIVATE Resources/PrivacyInfo.xcprivacy)
set_source_files_properties(Resources/PrivacyInfo.xcprivacy PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

target_link_libraries(CowTermMobile PRIVATE
        CowTerm::TerminalView CowTerm::Multiplexer CowTerm::Phone CowTerm::Pairing eacp-ui
        "-framework Network" "-framework Security" "-framework CoreImage")
# The Mac's paste confirmation, reused as it is.
target_include_directories(CowTermMobile PRIVATE ${PROJECT_SOURCE_DIR}/Terminal)
# The cow icon in every iPhone size, from scripts/make-ios-app-icon.sh.
target_sources(CowTermMobile PRIVATE Resources/Assets.xcassets)
set_source_files_properties(Resources/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# A simulator build is not signed, so it has no keychain, where the phone keeps
# its ssh key. Xcode gives a simulator app its entitlements in this section of
# the binary; a device build's come from its signature instead.
if (CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
    target_link_options(CowTermMobile PRIVATE
            "LINKER:-sectcreate,__TEXT,__entitlements,${CMAKE_CURRENT_SOURCE_DIR}/Simulator.entitlements")
endif ()

# An Xcode archive keeps only the app, never the libraries it links.
set_target_properties(CowTermMobile PROPERTIES
        MACOSX_BUNDLE TRUE
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
        XCODE_ATTRIBUTE_SKIP_INSTALL NO)

# The Mac app's JetBrains Mono, embedded the same way.
res_embed_add(CowTermMobile DIRECTORY ${PROJECT_SOURCE_DIR}/Terminal/Resources)

eacp_set_gui_subsystem(CowTermMobile)
set_default_target_setting(CowTermMobile)
