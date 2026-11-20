#include "DiffViewScreen.h"
#include "FingerOnWindow.h"
#include "InterfaceOrientation.h"
#include "NoPaneYetShell.h"
#include "OverviewSamples.h"
#include "OverviewScreen.h"
#include "PaneScreen.h"
#include "PhoneDiffViewBinding.h"
#include "PhoneLaunch.h"
#include "PhoneOverviewBinding.h"
#include "PhonePushNotifications.h"
#include "PhonePaneBinding.h"
#include "SampleShell.h"
#include "ScreenStack.h"

#include "Multiplexer/DaemonClient.h"
#include "Multiplexer/DataDirectories.h"

#include <ThisBuildsPhoneStamp.h>

#include "TerminalView/GlyphAtlas.h"
#include "TerminalView/TerminalView.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Threads/Timer.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Graphics/Window/Window.h>

#include <algorithm>
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

// --diff-view-steps= starts once dv has drawn and leaves dv time to answer each step.
constexpr auto diffViewFirstStepDelay = Time::MS {18000};
constexpr auto diffViewStepInterval = Time::MS {2500};

// --tap-after-overview and --drag-after-menu: the overview or menu opens once
// the pane has drawn, stays for a screenshot, closes, and then a finger lands.
constexpr auto hiddenViewCheckOpenDelay = Time::MS {3000};
constexpr auto hiddenViewCheckCloseDelay = Time::MS {8000};
constexpr auto hiddenViewCheckFingerDelay = Time::MS {10000};

// Long enough for the host's project scan to answer and the list to be
// screenshotted before the project opens.
constexpr auto projectOpenDelay = Time::MS {10000};

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
    bool showsKeyRowSecondPage = false;
    bool switchesToSecondPane = false;
    bool opensDiffView = false;
    bool closesDiffView = false;
    bool showsClaudePrompt = false;
    bool tapsPaneAfterOverview = false;
    bool dragsPaneAfterMenu = false;
    std::vector<cowterm::mobile::DiffViewLaunchStep> diffViewSteps;
    bool showsOverview = false;
    std::string sessionToOpen;
    std::string projectToOpen;
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
    {"--key-row-symbols", &LaunchOptions::showsKeyRowSecondPage},
    {"--switch-to-second-pane", &LaunchOptions::switchesToSecondPane},
    {"--open-diff-view", &LaunchOptions::opensDiffView},
    {"--close-diff-view", &LaunchOptions::closesDiffView},
    {"--claude-prompt", &LaunchOptions::showsClaudePrompt},
    {"--overview", &LaunchOptions::showsOverview},
    {"--tap-after-overview", &LaunchOptions::tapsPaneAfterOverview},
    {"--drag-after-menu", &LaunchOptions::dragsPaneAfterMenu},
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
    constexpr auto openSessionPrefix = std::string_view {"--open-session="};
    constexpr auto openProjectPrefix = std::string_view {"--open-project="};
    constexpr auto searchPrefix = std::string_view {"--search="};
    constexpr auto diffViewStepsPrefix = std::string_view {"--diff-view-steps="};

    if (argument.starts_with(linkPrefix))
    {
        options.linkState =
            parseLinkState(argument.substr(linkPrefix.size())).value_or(options.linkState);
    }
    else if (argument.starts_with(openSessionPrefix))
    {
        options.sessionToOpen = std::string {argument.substr(openSessionPrefix.size())};
    }
    else if (argument.starts_with(openProjectPrefix))
    {
        options.projectToOpen = std::string {argument.substr(openProjectPrefix.size())};
    }
    else if (argument.starts_with(diffViewStepsPrefix))
    {
        options.diffViewSteps = cowterm::mobile::parseDiffViewLaunchSteps(argument.substr(diffViewStepsPrefix.size()));
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
    pane.hostName = "tamby";
    pane.sessionName = "Grazer";
    pane.previousSessionName = "CowTerm";
    pane.paneCount = std::max(paneCount, 1);
    return pane;
}

Graphics::WindowOptions makeWindowOptions()
{
    auto options = Graphics::WindowOptions {};
    options.statusBar = Graphics::SystemStatusBar::LightText;
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

    return "tamby: connection lost; reconnecting\xe2\x80\xa6";
}

std::unique_ptr<cowterm::mobile::PhoneLaunch> makePhoneLaunch(const LaunchOptions& launchOptions)
{
    if (!launchOptions.phoneSettings)
        return nullptr;

    term::DaemonClient::setClientBuildStamp(term::getThisBuildsPhoneStamp());
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

struct CowTermMobileApp
{
    explicit CowTermMobileApp(const LaunchOptions& launchOptionsToUse)
        : launchOptions(launchOptionsToUse)
        , phone(makePhoneLaunch(launchOptions))
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
            lastKeyboardChange = change;
            screen.setSoftwareKeyboardChange(change);

            if (auto diffView = screens.findDiffView())
                diffView->get().setSoftwareKeyboardChange(change);

            if (auto overview = screens.findOverview())
                overview->get().setSoftwareKeyboardCoveredHeight(change.coveredHeight);
        };

        if (phone != nullptr)
        {
            phoneBinding.emplace(*phone, screen, [this] { showPaneScreen(); });
            auto switches = cowterm::mobile::ScreenSwitches {
                [this]() -> cowterm::mobile::DiffViewScreen& { return showDiffViewScreen(); },
                [this] { showPaneScreen(); }};
            diffViewBinding.emplace(*phone, screen, std::move(switches));
        }
        else if (launchOptions.showsSamples)
            showSamplePane();

        connectOverview();
        applyLaunchOptions();
        startPushNotifications();
    }

    // Once the first screen is up, so the question about notifications has
    // the app behind it rather than a blank window.
    void startPushNotifications()
    {
        constexpr auto pushStartDelay = Time::MS {2000};

        if (phone == nullptr)
            return;

        const auto& settings = phone->getSettings();
        pushNotifications.emplace(cowterm::mobile::PushRegistrationSettings {
            settings.pushRelayAddress, settings.deviceName, COWTERM_IOS_APNS_ENVIRONMENT});
        pushNotifications->onOpened = [this](const term::phone::NotificationTarget& target)
        { openNotificationTarget(target); };
        Threads::callAfter(pushStartDelay, [this] { pushNotifications->start(); });
    }

    void openNotificationTarget(const term::phone::NotificationTarget& target)
    {
        if (!phoneOverview)
            return;

        showOverview();
        phoneOverview->openNotificationTargetOnceListed(target);
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
        if (launchOptions.showsKeyRowSecondPage)
            screen.showKeyRowSecondPage();

        if (auto overview = screens.findOverview(); overview && !launchOptions.searchQuery.empty())
            overview->get().search(launchOptions.searchQuery);

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

        if (!launchOptions.projectToOpen.empty() && phoneOverview)
        {
            Threads::callAfter(projectOpenDelay,
                               [this] { phoneOverview->openSessionOnceListed(launchOptions.projectToOpen); });
        }

        if (launchOptions.opensDiffView)
            Threads::callAfter(diffViewOpenDelay, [this] { screen.actions.onDiffView(); });

        if (launchOptions.closesDiffView)
            Threads::callAfter(diffViewCloseDelay, [this] { closeDiffView(); });

        scheduleDiffViewSteps();
        scheduleHiddenViewChecks();

        if (launchOptions.scrollsBack)
        {
            Threads::callAfter(scrollBackDelay,
                               [this] { dragPaneDownToScrollBack(screen.getTerminal()); });
        }
    }

    void scheduleDiffViewSteps()
    {
        auto delay = diffViewFirstStepDelay;

        for (const auto& step: launchOptions.diffViewSteps)
        {
            Threads::callAfter(delay,
                               [this, step]
                               {
                                   if (auto diffView = screens.findDiffView())
                                       diffView->get().performLaunchStep(step);
                               });
            delay.count += diffViewStepInterval.count;
        }
    }

    void closeDiffView()
    {
        if (auto diffView = screens.findDiffView())
            diffView->get().close();
    }

    // --tap-after-overview and --drag-after-menu: a screen or menu comes and
    // goes, then a finger lands where it was, routed from the window's root as a
    // real touch is. The pane screen logs what reaches it.
    void scheduleHiddenViewChecks()
    {
        if (launchOptions.tapsPaneAfterOverview)
        {
            Threads::callAfter(hiddenViewCheckOpenDelay, [this] { showOverview(); });
            Threads::callAfter(hiddenViewCheckCloseDelay, [this] { showPaneScreen(); });
            Threads::callAfter(hiddenViewCheckFingerDelay, [this] { tapPaneCentre(); });
        }

        if (launchOptions.dragsPaneAfterMenu)
        {
            Threads::callAfter(hiddenViewCheckOpenDelay, [this] { screen.openMenu(); });
            Threads::callAfter(hiddenViewCheckCloseDelay, [this] { screen.closeMenu(); });
            Threads::callAfter(hiddenViewCheckFingerDelay, [this] { dragPaneDown(); });
        }
    }

    void tapPaneCentre()
    {
        LOG("hidden view check: a tap on the pane's centre");
        cowterm::mobile::tapWithFingerOnWindow(screens, screen.getTerminal().getBounds().center());
    }

    void dragPaneDown()
    {
        constexpr auto dragDistance = 240.0f;
        const auto start = screen.getTerminal().getBounds().center();
        LOG("hidden view check: a drag down the pane");
        cowterm::mobile::moveFingerOnWindow(screens, start, {start.x, start.y + dragDistance});
    }

    // The hosts' sessions, or the sample rows with --sample; with neither, the
    // overview says no hosts are set up.
    void connectOverview()
    {
        screen.actions.onOverview = [this] { showOverview(); };

        if (phone != nullptr)
        {
            phoneOverview.emplace(*phone, [this] { showPaneScreen(); });
            phoneOverview->openSessionOnceListed(launchOptions.sessionToOpen);
        }

        if (isShowingOverviewFirst())
            showOverview();
        else
            tellPhoneWhetherThePaneScreenIsUp(true);
    }

    // While the pane screen is up the phone tells the pane's daemon Jamie is
    // viewing it, which reads it (PhoneConnection::setPaneScreenShown).
    void tellPhoneWhetherThePaneScreenIsUp(bool isPaneScreenUp)
    {
        if (phone != nullptr)
            phone->setPaneScreenShown(isPaneScreenUp);
    }

    // A new overview each time, so its search is empty; its scroll is kept.
    void showOverview()
    {
        LOG("overview: shown");
        tellPhoneWhetherThePaneScreenIsUp(false);
        auto& overview = screens.showOverview();
        overview.setSoftwareKeyboardCoveredHeight(lastKeyboardChange.coveredHeight);

        if (phoneOverview)
            phoneOverview->showOn(overview);
        else
            showSampleOverview(overview);

        overview.setScrollOffset(overviewScrollOffset);
    }

    void showSampleOverview(cowterm::mobile::OverviewScreen& overview)
    {
        if (!launchOptions.showsSamples)
            return overview.setInputs({});

        overview.setInputs(cowterm::mobile::makeSampleOverviewInputs());
        overview.onOpenSession = [this](const term::overview::OverviewRow& session)
        {
            LOG("overview: open ", session.host, "/", session.sessionName);
            showPaneScreen();
        };
    }

    void showPaneScreen()
    {
        leaveOverview();
        screens.showPaneScreen();
        tellPhoneWhetherThePaneScreenIsUp(true);
    }

    cowterm::mobile::DiffViewScreen& showDiffViewScreen()
    {
        leaveOverview();
        tellPhoneWhetherThePaneScreenIsUp(false);
        auto& diffView = screens.showDiffView();
        diffView.setSoftwareKeyboardChange(lastKeyboardChange);
        return diffView;
    }

    void leaveOverview()
    {
        if (auto overview = screens.findOverview())
            overviewScrollOffset = overview->get().getScrollOffset();

        if (phoneOverview)
            phoneOverview->stopShowing();
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
    std::unique_ptr<cowterm::mobile::PhoneLaunch> phone;
    cowterm::mobile::PaneScreen screen;
    std::optional<cowterm::mobile::PhonePaneBinding> phoneBinding;
    std::optional<cowterm::mobile::PhoneOverviewBinding> phoneOverview;
    std::optional<cowterm::mobile::PhoneDiffViewBinding> diffViewBinding;
    std::optional<cowterm::mobile::PhonePushNotifications> pushNotifications;
    cowterm::mobile::ScreenStack screens {screen};
    Graphics::Window window {screens, makeWindowOptions()};
    Graphics::SoftwareKeyboardChange lastKeyboardChange;
    float overviewScrollOffset = 0.0f;
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
    startLogFile();
    term::registerEmbeddedFonts();

    return Apps::run<CowTermMobileApp>(parseLaunchOptions(argc, argv));
}
