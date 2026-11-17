#include "DiffViewScreen.h"
#include "InterfaceOrientation.h"
#include "NoPaneYetShell.h"
#include "OverviewSamples.h"
#include "OverviewScreen.h"
#include "PairingScreen.h"
#include "PaneScreen.h"
#include "PhoneDiffViewBinding.h"
#include "PhoneLaunch.h"
#include "PhoneOverviewBinding.h"
#include "PhonePaneBinding.h"
#include "PhonePairing.h"
#include "SampleShell.h"

#include "Multiplexer/DataDirectories.h"

#include "TerminalView/GlyphAtlas.h"
#include "TerminalView/TerminalView.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Threads/Timer.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Graphics/Window/Window.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string_view>

using namespace eacp;

namespace
{
constexpr auto samplePaneCount = 3;
constexpr auto scrollBackDelay = Time::MS {5000};

// Long enough for a link to a host to come up and its session to be listed.
constexpr auto paneSwitchDelay = Time::MS {10000};
constexpr auto secondPaneIndex = 1;

// The diff button pressed after the pane is on screen, and dv closed once it has drawn.
constexpr auto diffViewOpenDelay = Time::MS {10000};
constexpr auto diffViewCloseDelay = Time::MS {20000};

// Long enough for the keyboard to have come up, so the composer is on screen.
constexpr auto composeDelay = Time::MS {1500};

// Read back from the phone with `xcrun devicectl device copy from`.
constexpr auto logFileName = "CowTermMobile.log";
constexpr auto previousLogFileName = "CowTermMobile.previous.log";

// Test hooks, from the launch arguments, for what a headless simulator cannot
// do by hand.
struct LaunchOptions
{
    // The invented pane and overview the screenshot scripts take, in place of any host.
    bool showsSamples = false;
    bool raisesSoftwareKeyboard = false;
    bool turnsToLandscape = false;
    bool turnsToPortrait = false;
    bool opensMenu = false;
    bool streamsOutput = false;
    bool scrollsBack = false;
    bool showsComposer = false;
    bool showsKeyRowSecondPage = false;
    bool switchesToSecondPane = false;
    bool opensDiffView = false;
    bool closesDiffView = false;
    bool showsClaudePrompt = false;
    std::string composedText;
    bool showsOverview = false;
    std::string sessionToOpen;
    std::string searchQuery;
    cowterm::mobile::LinkState linkState = cowterm::mobile::LinkState::Live;
    int paneCount = samplePaneCount;
    std::optional<term::phone::PhoneSettings> phoneSettings;
};

std::optional<cowterm::mobile::LinkState> parseLinkState(std::string_view name)
{
    using cowterm::mobile::LinkState;

    if (name == "live")
        return LinkState::Live;

    if (name == "connecting")
        return LinkState::Connecting;

    if (name == "stalled")
        return LinkState::Stalled;

    return std::nullopt;
}

struct LaunchSwitch
{
    std::string_view name;
    bool LaunchOptions::*member;
};

constexpr LaunchSwitch launchSwitches[] = {
    {"--sample", &LaunchOptions::showsSamples},
    {"--raise-keyboard", &LaunchOptions::raisesSoftwareKeyboard},
    {"--landscape", &LaunchOptions::turnsToLandscape},
    {"--portrait", &LaunchOptions::turnsToPortrait},
    {"--menu", &LaunchOptions::opensMenu},
    {"--stream", &LaunchOptions::streamsOutput},
    {"--scroll-back", &LaunchOptions::scrollsBack},
    {"--composer", &LaunchOptions::showsComposer},
    {"--key-row-symbols", &LaunchOptions::showsKeyRowSecondPage},
    {"--switch-to-second-pane", &LaunchOptions::switchesToSecondPane},
    {"--open-diff-view", &LaunchOptions::opensDiffView},
    {"--close-diff-view", &LaunchOptions::closesDiffView},
    {"--claude-prompt", &LaunchOptions::showsClaudePrompt},
    {"--overview", &LaunchOptions::showsOverview},
};

bool applyLaunchSwitch(LaunchOptions& options, std::string_view argument)
{
    for (const auto& launchSwitch: launchSwitches)
    {
        if (argument == launchSwitch.name)
        {
            options.*launchSwitch.member = true;
            return true;
        }
    }

    return false;
}

void applyLaunchValue(LaunchOptions& options, std::string_view argument)
{
    constexpr auto linkPrefix = std::string_view {"--link="};
    constexpr auto panesPrefix = std::string_view {"--panes="};
    constexpr auto composePrefix = std::string_view {"--compose="};
    constexpr auto openSessionPrefix = std::string_view {"--open-session="};
    constexpr auto searchPrefix = std::string_view {"--search="};

    if (argument.starts_with(linkPrefix))
    {
        options.linkState =
            parseLinkState(argument.substr(linkPrefix.size())).value_or(options.linkState);
    }
    else if (argument.starts_with(composePrefix))
    {
        options.composedText = std::string {argument.substr(composePrefix.size())};
        std::ranges::replace(options.composedText, '|', '\n');
    }
    else if (argument.starts_with(openSessionPrefix))
    {
        options.sessionToOpen = std::string {argument.substr(openSessionPrefix.size())};
    }
    else if (argument.starts_with(searchPrefix))
    {
        options.searchQuery = std::string {argument.substr(searchPrefix.size())};
    }
    else if (argument.starts_with(panesPrefix))
    {
        const auto count = std::string {argument.substr(panesPrefix.size())};
        options.paneCount = std::atoi(count.c_str());
    }
}

LaunchOptions parseLaunchOptions(int argc, char* argv[])
{
    auto options = LaunchOptions {};

    for (auto index = 1; index < argc; ++index)
    {
        const auto argument = std::string_view {argv[index]};

        if (!applyLaunchSwitch(options, argument))
            applyLaunchValue(options, argument);
    }

    if (!options.showsSamples)
        options.phoneSettings = cowterm::mobile::PhoneLaunch::readSettings(argc, argv);

    return options;
}

cowterm::mobile::PaneDescription makeSamplePaneDescription(int paneCount)
{
    auto pane = cowterm::mobile::PaneDescription {};
    pane.hostName = "laptop";
    pane.sessionName = "Grazer";
    pane.previousSessionName = "CowTerm";
    pane.paneCount = std::max(paneCount, 1);
    return pane;
}

Graphics::WindowOptions makeWindowOptions()
{
    auto options = Graphics::WindowOptions {};
    options.statusBar = Graphics::StatusBar::LightText;
    return options;
}

// A finger dragging the pane down by scrollBackDistance and letting go: what
// a reader does to scroll back, for a screenshot of the "n new" pill.
void dragPaneDownToScrollBack(term::TerminalView& terminal)
{
    constexpr auto scrollBackDistance = 240.0f;
    constexpr auto fingerId = 1;
    const auto start = Graphics::Point {terminal.getLocalBounds().w / 2.0f, 100.0f};

    auto touch = Graphics::TouchEvent {};
    touch.id = fingerId;
    touch.pos = start;
    touch.downPos = start;
    terminal.beginTouchScroll(touch);

    touch.phase = Graphics::TouchPhase::Moved;
    touch.pos = {start.x, start.y + scrollBackDistance};
    terminal.moveTouchScroll(touch);
    terminal.endTouchScrollWithVelocity(0.0f);
}

// The note a daemon pane shows over its text when its link is not live.
std::string describeSampleStatusNote(cowterm::mobile::LinkState state)
{
    if (state == cowterm::mobile::LinkState::Live)
        return {};

    return "laptop: connection lost; reconnecting\xe2\x80\xa6";
}

std::unique_ptr<cowterm::mobile::PhoneLaunch> makePhoneLaunch(const LaunchOptions& launchOptions)
{
    if (!launchOptions.phoneSettings)
        return nullptr;

    return std::make_unique<cowterm::mobile::PhoneLaunch>(*launchOptions.phoneSettings);
}

// The daemon's pane when the launch named one; the sample shell otherwise.
bool isOpeningAPhonePane(const std::unique_ptr<cowterm::mobile::PhoneLaunch>& phone)
{
    return phone != nullptr && !phone->getPaneToOpen().empty();
}

term::TerminalConfig makePaneTerminalConfig(const std::unique_ptr<cowterm::mobile::PhoneLaunch>& phone,
                                            const LaunchOptions& launchOptions)
{
    if (isOpeningAPhonePane(phone))
        return cowterm::mobile::PhonePaneBinding::makePaneConfig(*phone, {phone->getHost(), phone->getPaneToOpen()});

    if (!launchOptions.showsSamples)
        return cowterm::mobile::makeNoPaneYetConfig();

    auto sample = cowterm::mobile::SampleShellOptions {};
    sample.streamsOutput = launchOptions.streamsOutput;
    sample.statusNote = describeSampleStatusNote(launchOptions.linkState);
    sample.showsClaudePrompt = launchOptions.showsClaudePrompt;
    return cowterm::mobile::makeSampleShellConfig(sample);
}

std::string getPaneId(const std::unique_ptr<cowterm::mobile::PhoneLaunch>& phone)
{
    if (isOpeningAPhonePane(phone))
        return phone->getPaneToOpen();

    return {};
}

enum class FirstScreen
{
    PaneScreen,
    Overview,
    Pairing
};

// The views ScreenStack switches between.
struct StackedScreens
{
    Graphics::View& paneScreen;
    Graphics::View& overview;
    Graphics::View& diffView;
    Graphics::View& pairing;
};

// The window's one content view: the pane screen, the overview, the diff view
// and the pairing screen all live in it, and it shows one. eacp sends every
// touch to the window's root view and hit-tests from there, so the root never
// changes.
class ScreenStack final : public Graphics::View
{
public:
    ScreenStack(StackedScreens screensToUse, FirstScreen firstScreen)
        : screens(screensToUse)
    {
        addChildren({screens.paneScreen, screens.overview, screens.diffView, screens.pairing});
        show(chooseScreen(firstScreen));
    }

    void showPaneScreen() { show(screens.paneScreen); }
    void showOverview() { show(screens.overview); }
    void showDiffView() { show(screens.diffView); }

    void resized() override
    {
        for (auto* screen: listScreens())
            screen->setBounds(getLocalBounds());
    }

private:
    std::array<Graphics::View*, 4> listScreens()
    {
        return {&screens.paneScreen, &screens.overview, &screens.diffView, &screens.pairing};
    }

    Graphics::View& chooseScreen(FirstScreen firstScreen)
    {
        switch (firstScreen)
        {
            case FirstScreen::Overview:
                return screens.overview;
            case FirstScreen::Pairing:
                return screens.pairing;
            case FirstScreen::PaneScreen:
                break;
        }

        return screens.paneScreen;
    }

    void show(Graphics::View& shown)
    {
        for (auto* screen: listScreens())
            screen->setVisible(screen == &shown);
    }

    StackedScreens screens;
};

struct CowTermMobileApp
{
    explicit CowTermMobileApp(const LaunchOptions& launchOptionsToUse)
        : launchOptions(launchOptionsToUse)
        , deviceKey(cowterm::mobile::loadOrCreateDeviceKey())
        , phone(makePhoneLaunch(useDeviceKey(launchOptions, deviceKey)))
        , screen(makePaneTerminalConfig(phone, launchOptions), getPaneId(phone))
    {
        Apps::setSuspendHandler(
            [this](bool suspended)
            {
                if (phone != nullptr)
                    phone->handleSuspended(suspended);

                if (suspended)
                    LOG("lifecycle: entered background");
                else
                    LOG("lifecycle: back in foreground");
            });

        window.events.onSoftwareKeyboardChanged =
            [this](const Graphics::SoftwareKeyboardChange& change)
        {
            LOG("software keyboard covers ",
                change.coveredHeight,
                " points, animating ",
                change.animationSeconds,
                " s");
            screen.setSoftwareKeyboardChange(change);
            overview.setSoftwareKeyboardCoveredHeight(change.coveredHeight);
            pairing.setSoftwareKeyboardCoveredHeight(change.coveredHeight);
        };

        if (phone != nullptr)
            bindPaneScreenToPhone();
        else if (launchOptions.showsSamples)
            showSamplePane();

        connectOverview();
        connectPairing();
        applyLaunchOptions();
    }

    // With no host set up, the pairing screen comes first; once a computer is
    // added the phone starts on the hosts it now has.
    void connectPairing()
    {
        pairing.getPanel().onPaired = [this] { startPhoneAfterPairing(); };
        LOG("pairing: this phone's key: ", cowterm::mobile::describeDeviceKeyLine(deviceKey));

        const auto& launchArguments = Apps::commandLineArgs();
        const auto arguments = std::vector<std::string> {launchArguments.begin(), launchArguments.end()};
        const auto flags = cowterm::mobile::parsePairingLaunchFlags(arguments);

        if (flags.trustsOfferedHostKey)
            pairing.getPanel().trustOfferedHostKeyOnceShown();

        if (!flags.hostToPairWith.empty())
            pairing.getPanel().connectTo(flags.hostToPairWith);
    }

    void startPhoneAfterPairing()
    {
        auto programName = std::string {"CowTermMobile"};
        auto* arguments = programName.data();
        auto settings = cowterm::mobile::PhoneLaunch::readSettings(1, &arguments);
        settings = cowterm::mobile::useDeviceKeyWithoutKeyFile(std::move(settings), deviceKey);

        if (!settings)
            return LOG("pairing: paired, but no host could be read back from the pairing folder");

        phoneOverview.reset();
        diffViewBinding.reset();
        phoneBinding.reset();
        phone = std::make_unique<cowterm::mobile::PhoneLaunch>(std::move(*settings));
        bindPaneScreenToPhone();
        phoneOverview.emplace(*phone, overview, [this] { screens.showPaneScreen(); });
        showOverview();
    }

    void bindPaneScreenToPhone()
    {
        phoneBinding.emplace(*phone, screen, [this] { screens.showPaneScreen(); });
        auto switches = cowterm::mobile::ScreenSwitches {[this] { screens.showDiffView(); },
                                                         [this] { screens.showPaneScreen(); }};
        diffViewBinding.emplace(*phone, screen, diffView, std::move(switches));
    }

    static LaunchOptions useDeviceKey(LaunchOptions options, const std::optional<cowterm::mobile::DeviceKey>& key)
    {
        options.phoneSettings = cowterm::mobile::useDeviceKeyWithoutKeyFile(std::move(options.phoneSettings), key);
        return options;
    }

    bool isWaitingForPairing() const { return phone == nullptr && !launchOptions.showsSamples; }

    FirstScreen chooseFirstScreen() const
    {
        if (isWaitingForPairing())
            return FirstScreen::Pairing;

        if (isShowingOverviewFirst())
            return FirstScreen::Overview;

        return FirstScreen::PaneScreen;
    }

    void showSamplePane()
    {
        auto pane = makeSamplePaneDescription(launchOptions.paneCount);
        screen.setPaneDescription(pane);
        screen.setLinkStatus({launchOptions.linkState, sampleLinkPictureTime()});

        screen.actions.onChoosePane = [this, pane](int index) mutable
        {
            pane.activePaneIndex = index;
            screen.setPaneDescription(pane);
        };
    }

    void applyLaunchOptions()
    {
        applyLaunchKeyboardOptions();
        applyLaunchOrientation();
        scheduleLaunchActions();

        if (launchOptions.opensMenu)
            screen.openMenu();
    }

    void applyLaunchKeyboardOptions()
    {
        if (launchOptions.showsComposer)
            screen.switchToComposer();

        if (launchOptions.showsKeyRowSecondPage)
            screen.showKeyRowSecondPage();

        if (!launchOptions.searchQuery.empty())
            overview.search(launchOptions.searchQuery);

        if (launchOptions.raisesSoftwareKeyboard)
            screen.raiseKeyboard();
    }

    void applyLaunchOrientation()
    {
        if (launchOptions.turnsToLandscape)
            cowterm::mobile::requestLandscapeInterfaceOrientation(window);

        if (launchOptions.turnsToPortrait)
            cowterm::mobile::requestPortraitInterfaceOrientation(window);
    }

    // Test flags that act once the screen has settled.
    void scheduleLaunchActions()
    {
        if (launchOptions.switchesToSecondPane)
        {
            Threads::callAfter(paneSwitchDelay,
                               [this] { screen.actions.onChoosePane(secondPaneIndex); });
        }

        if (launchOptions.opensDiffView)
            Threads::callAfter(diffViewOpenDelay, [this] { screen.actions.onDiffView(); });

        if (launchOptions.closesDiffView)
            Threads::callAfter(diffViewCloseDelay, [this] { diffView.close(); });

        if (!launchOptions.composedText.empty())
        {
            Threads::callAfter(composeDelay,
                               [this] { screen.composeAndSend(launchOptions.composedText); });
        }

        if (launchOptions.scrollsBack)
        {
            Threads::callAfter(scrollBackDelay,
                               [this] { dragPaneDownToScrollBack(screen.getTerminal()); });
        }
    }

    // The hosts' sessions, or the sample rows with --sample; with neither, the
    // overview says no hosts are set up.
    void connectOverview()
    {
        screen.actions.onOverview = [this] { showOverview(); };

        if (phone != nullptr)
        {
            phoneOverview.emplace(*phone, overview, [this] { screens.showPaneScreen(); });
            phoneOverview->openSessionOnceListed(launchOptions.sessionToOpen);
            phoneOverview->setShown(isShowingOverviewFirst());
            return;
        }

        if (!launchOptions.showsSamples)
            return overview.setInputs({});

        overview.setInputs(cowterm::mobile::makeSampleOverviewInputs());
        overview.onOpenSession = [this](const term::overview::OverviewRow& session)
        {
            LOG("overview: open ", session.host, "/", session.sessionName);
            screens.showPaneScreen();
        };
    }

    void showOverview()
    {
        LOG("overview: shown");
        overview.clearSearch();
        if (phoneOverview)
            phoneOverview->setShown(true);

        screens.showOverview();
    }

    // The pane screen comes first only with a pane to show: a named one, or the sample.
    bool isShowingOverviewFirst() const
    {
        if (launchOptions.showsOverview)
            return true;

        if (launchOptions.showsSamples)
            return false;

        return !isOpeningAPhonePane(phone);
    }

    // A picture taken twelve seconds before launch, so a pane that is not
    // live has an age to show.
    static std::chrono::steady_clock::time_point sampleLinkPictureTime()
    {
        constexpr auto samplePictureAge = std::chrono::seconds {12};
        return std::chrono::steady_clock::now() - samplePictureAge;
    }

    LaunchOptions launchOptions;
    std::optional<cowterm::mobile::DeviceKey> deviceKey;
    std::unique_ptr<cowterm::mobile::PhoneLaunch> phone;
    cowterm::mobile::PaneScreen screen;
    std::optional<cowterm::mobile::PhonePaneBinding> phoneBinding;
    std::optional<cowterm::mobile::PhoneOverviewBinding> phoneOverview;
    std::optional<cowterm::mobile::PhoneDiffViewBinding> diffViewBinding;
    cowterm::mobile::OverviewScreen overview;
    cowterm::mobile::DiffViewScreen diffView;
    term::pairing::PairedHosts pairedHosts {cowterm::mobile::PhoneLaunch::getPairingFolder()};
    cowterm::mobile::PairingScreen pairing {pairedHosts,
                                            cowterm::mobile::describeDeviceKeyLine(deviceKey),
                                            cowterm::mobile::fetchHostKeyInBackground};
    ScreenStack screens {{screen, overview, diffView, pairing}, chooseFirstScreen()};
    Graphics::Window window {screens, makeWindowOptions()};
};
} // namespace

// Each launch starts the log afresh and keeps the one before, so a relaunch
// never loses what the last run did.
void startLogFile()
{
    const auto logPath = term::getLogsDirectory() / logFileName;
    auto error = std::error_code {};
    std::filesystem::rename(logPath, term::getLogsDirectory() / previousLogFileName, error);
    eacp::setLogFile(logPath.string());
}

int main(int argc, char* argv[])
{
    Apps::setCommandLineArgs(argc, argv);
    startLogFile();
    term::registerEmbeddedFonts();

    return Apps::run<CowTermMobileApp>(parseLaunchOptions(argc, argv));
}
