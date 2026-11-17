#include "InterfaceOrientation.h"
#include "OverviewSamples.h"
#include "OverviewScreen.h"
#include "PaneScreen.h"
#include "PhoneLaunch.h"
#include "PhoneOverviewBinding.h"
#include "PhonePaneBinding.h"

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
#include <optional>
#include <string_view>

using namespace eacp;

namespace
{
constexpr auto samplePaneCount = 3;
constexpr auto streamedLinesPerSecond = 10;
constexpr auto scrollBackDelay = Time::MS {5000};

// Long enough for a link to a host to come up and its session to be listed.
constexpr auto paneSwitchDelay = Time::MS {10000};
constexpr auto secondPaneIndex = 1;

// Long enough for the keyboard to have come up, so the composer is on screen.
constexpr auto composeDelay = Time::MS {1500};

constexpr std::string_view sampleScreenText =
    "\x1b[1;32mjamie@tamby\x1b[0m:\x1b[1;34m~/projects/CowTerm\x1b[0m$ git status\r\n"
    "On branch \x1b[1mjp/ios-app\x1b[0m\r\n"
    "Changes not staged for commit:\r\n"
    "  \x1b[31mmodified:   Lib/Shell/CMakeLists.txt\x1b[0m\r\n"
    "\x1b[32mUntracked files:\x1b[0m\r\n"
    "  \x1b[32miOS/\x1b[0m\r\n"
    "\r\n"
    "\x1b[1;32mjamie@tamby\x1b[0m:\x1b[1;34m~/projects/CowTerm\x1b[0m$ ls\r\n"
    "\x1b[1;34mDaemon\x1b[0m  \x1b[1;34mLib\x1b[0m  \x1b[1;34mRemote\x1b[0m  "
    "\x1b[1;34mTerminal\x1b[0m  \x1b[1;34miOS\x1b[0m  CMakeLists.txt\r\n"
    "\r\n"
    "\xe2\x94\x8c\xe2\x94\x80 CowTerm for iPhone \xe2\x94\x80\xe2\x94\x90\r\n"
    "\xe2\x94\x82 \x1b[33mno network yet\x1b[0m       \xe2\x94\x82\r\n"
    "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
    "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
    "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
    "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\r\n"
    "\r\n"
    "\x1b[1;32mjamie@tamby\x1b[0m:\x1b[1;34m~/projects/CowTerm\x1b[0m$ ";

// Stands in for the daemon until the ssh link exists: the pane shows
// sampleScreenText, then, when asked to stream, a numbered line ten times a
// second, and logs what is typed into it.
class SampleTextShell final : public term::Shell
{
public:
    SampleTextShell(bool streamsOutputToUse, std::string statusNoteToUse)
        : streamsOutput(streamsOutputToUse)
        , statusNote(std::move(statusNoteToUse))
    {
    }

    bool start(const term::PtyOptions&,
               std::function<void(std::string)> onOutputToUse,
               std::function<void()>) override
    {
        onOutput = std::move(onOutputToUse);
        onOutput(std::string {sampleScreenText});
        onStatusChanged(statusNote);

        if (streamsOutput)
            streamTimer.emplace([this] { writeNextStreamedLine(); }, streamedLinesPerSecond);

        return true;
    }

    void write(std::string_view data) override { LOG("typed ", data.size(), " bytes"); }
    void resize(const term::PtySize&) override {}

    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }

    void terminate() override { streamTimer.reset(); }
    void detach() override { streamTimer.reset(); }

private:
    void writeNextStreamedLine()
    {
        ++streamedLineCount;
        onOutput("output line " + std::to_string(streamedLineCount) + "\r\n");
    }

    bool streamsOutput = false;
    std::string statusNote;
    int streamedLineCount = 0;
    std::function<void(std::string)> onOutput = [](std::string) {};
    std::optional<Threads::Timer> streamTimer;
};

term::TerminalConfig makeSampleTextTerminalConfig(bool streamsOutput, const std::string& statusNote)
{
    auto config = term::TerminalConfig {};
    config.makeShell = [streamsOutput, statusNote](const std::string&)
    { return std::make_unique<SampleTextShell>(streamsOutput, statusNote); };
    return config;
}

// Test hooks, from the launch arguments, for what a headless simulator cannot
// do by hand.
struct LaunchOptions
{
    bool raisesSoftwareKeyboard = false;
    bool turnsToLandscape = false;
    bool opensMenu = false;
    bool streamsOutput = false;
    bool scrollsBack = false;
    bool typesRaw = false;
    bool switchesToSecondPane = false;
    std::string composedText;
    bool showsOverview = false;
    std::string sessionToOpen;
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
    {"--raise-keyboard", &LaunchOptions::raisesSoftwareKeyboard},
    {"--landscape", &LaunchOptions::turnsToLandscape},
    {"--menu", &LaunchOptions::opensMenu},
    {"--stream", &LaunchOptions::streamsOutput},
    {"--scroll-back", &LaunchOptions::scrollsBack},
    {"--raw", &LaunchOptions::typesRaw},
    {"--switch-to-second-pane", &LaunchOptions::switchesToSecondPane},
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
    else if (argument.starts_with(panesPrefix))
    {
        const auto count = std::string {argument.substr(panesPrefix.size())};
        options.paneCount = std::atoi(count.c_str());
    }
}

LaunchOptions parseLaunchOptions(int argc, char* argv[])
{
    auto options = LaunchOptions {};
    options.phoneSettings = cowterm::mobile::PhoneLaunch::readSettings(argc, argv);

    for (auto index = 1; index < argc; ++index)
    {
        const auto argument = std::string_view {argv[index]};

        if (!applyLaunchSwitch(options, argument))
            applyLaunchValue(options, argument);
    }

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

    return "tamby: connection lost; reconnecting\xe2\x80\xa6";
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
        return cowterm::mobile::PhonePaneBinding::makeFirstPaneConfig(*phone);

    return makeSampleTextTerminalConfig(launchOptions.streamsOutput,
                                        describeSampleStatusNote(launchOptions.linkState));
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
            screen.setSoftwareKeyboardChange(change);
        };

        if (phone != nullptr)
            phoneBinding.emplace(*phone, screen);
        else
            showSamplePane();

        connectOverview();
        applyLaunchOptions();
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
        if (launchOptions.typesRaw)
            screen.switchToRawMode();

        if (launchOptions.raisesSoftwareKeyboard)
            screen.raiseKeyboard();

        if (launchOptions.switchesToSecondPane)
        {
            Threads::callAfter(paneSwitchDelay,
                               [this] { screen.actions.onChoosePane(secondPaneIndex); });
        }

        if (!launchOptions.composedText.empty())
        {
            Threads::callAfter(composeDelay,
                               [this] { screen.composeAndSend(launchOptions.composedText); });
        }

        if (launchOptions.turnsToLandscape)
            cowterm::mobile::requestLandscapeInterfaceOrientation(window);

        if (launchOptions.opensMenu)
            screen.openMenu();

        if (launchOptions.scrollsBack)
        {
            Threads::callAfter(scrollBackDelay,
                               [this] { dragPaneDownToScrollBack(screen.getTerminal()); });
        }

    }

    // Sample rows until the connection layer feeds real ones; opening a
    // session goes back to the pane screen.
    void connectOverview()
    {
        overview.setInputs(cowterm::mobile::makeSampleOverviewInputs());
        overview.onOpenSession = [this](const term::overview::OverviewRow& session)
        {
            LOG("overview: open ", session.host, "/", session.sessionName);
            window.setContentView(screen);
        };
        screen.actions.onOverview = [this] { showOverview(); };

        if (phone != nullptr)
        {
            phoneOverview.emplace(*phone,
                                  overview,
                                  [this](const std::string& paneId)
                                  {
                                      screen.showPane(phone->makeTerminalConfig(), paneId);
                                      window.setContentView(screen);
                                  });

            phoneOverview->openSessionOnceListed(launchOptions.sessionToOpen);
            phoneOverview->setShown(launchOptions.showsOverview);
        }
    }

    void showOverview()
    {
        if (phoneOverview)
            phoneOverview->setShown(true);

        window.setContentView(overview);
    }

    Graphics::View& chooseRootView()
    {
        if (launchOptions.showsOverview)
            return overview;

        return screen;
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
    cowterm::mobile::OverviewScreen overview;
    Graphics::Window window {chooseRootView(), makeWindowOptions()};
};
} // namespace

int main(int argc, char* argv[])
{
    term::registerEmbeddedFonts();

    return Apps::run<CowTermMobileApp>(parseLaunchOptions(argc, argv));
}
