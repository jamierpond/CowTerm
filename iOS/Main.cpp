#include "InterfaceOrientation.h"
#include "PhoneLaunch.h"

#include "TerminalView/GlyphAtlas.h"
#include "TerminalView/TerminalView.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Graphics/Window/Window.h>

#include <string_view>

using namespace eacp;

namespace
{
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
// sampleScreenText once and logs what is typed into it.
class SampleTextShell final : public term::Shell
{
public:
    bool start(const term::PtyOptions&,
               std::function<void(std::string)> onOutput,
               std::function<void()>) override
    {
        onOutput(std::string {sampleScreenText});
        return true;
    }

    void write(std::string_view data) override { LOG("typed ", data.size(), " bytes"); }
    void resize(const term::PtySize&) override {}

    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }

    void terminate() override {}
    void detach() override {}
};

term::TerminalConfig makeSampleTextTerminalConfig()
{
    auto config = term::TerminalConfig {};
    config.makeShell = [](const std::string&)
    { return std::make_unique<SampleTextShell>(); };
    return config;
}

// Test hooks, from the launch arguments, for what a headless simulator cannot
// do by hand.
struct LaunchOptions
{
    bool raisesSoftwareKeyboard = false;
    bool turnsToLandscape = false;
    std::optional<term::phone::PhoneSettings> phoneSettings;
};

LaunchOptions parseLaunchOptions(int argc, char* argv[])
{
    auto options = LaunchOptions {};
    options.phoneSettings = cowterm::mobile::PhoneLaunch::readSettings(argc, argv);

    for (auto index = 1; index < argc; ++index)
    {
        const auto argument = std::string_view {argv[index]};

        if (argument == "--raise-keyboard")
            options.raisesSoftwareKeyboard = true;
        else if (argument == "--landscape")
            options.turnsToLandscape = true;
    }

    return options;
}

void logSafeAreaInsets(const Graphics::Insets& insets)
{
    LOG("safe area top ",
        insets.top,
        " left ",
        insets.left,
        " bottom ",
        insets.bottom,
        " right ",
        insets.right);
}

// The window's content: the terminal, kept inside the safe area so the
// status bar, the camera housing and the home indicator never cover text.
class MobileRootView final : public Graphics::View
{
public:
    MobileRootView(const term::TerminalConfig& config, const std::string& paneId)
        : terminal(config, {}, paneId)
    {
        addSubview(terminal);
    }

    void resized() override { placeTerminalInsideSafeArea(); }

    void safeAreaInsetsChanged() override
    {
        logSafeAreaInsets(getSafeAreaInsets());
        placeTerminalInsideSafeArea();
    }

    term::TerminalView terminal;

private:
    void placeTerminalInsideSafeArea()
    {
        terminal.setBounds(getLocalBounds().inset(getSafeAreaInsets()));
    }
};

Graphics::WindowOptions makeWindowOptions()
{
    auto options = Graphics::WindowOptions {};
    options.statusBar = Graphics::StatusBar::LightText;
    return options;
}

std::unique_ptr<cowterm::mobile::PhoneLaunch> makePhoneLaunch(const LaunchOptions& launchOptions)
{
    if (!launchOptions.phoneSettings)
        return nullptr;

    return std::make_unique<cowterm::mobile::PhoneLaunch>(*launchOptions.phoneSettings);
}

// The daemon's pane when the launch named one; the sample text otherwise.
bool isOpeningAPhonePane(const std::unique_ptr<cowterm::mobile::PhoneLaunch>& phone)
{
    return phone != nullptr && !phone->getPaneToOpen().empty();
}

term::TerminalConfig makeRootTerminalConfig(const std::unique_ptr<cowterm::mobile::PhoneLaunch>& phone)
{
    if (isOpeningAPhonePane(phone))
        return phone->makeTerminalConfig();

    return makeSampleTextTerminalConfig();
}

std::string getRootPaneId(const std::unique_ptr<cowterm::mobile::PhoneLaunch>& phone)
{
    if (isOpeningAPhonePane(phone))
        return phone->getPaneToOpen();

    return {};
}

struct CowTermMobileApp
{
    explicit CowTermMobileApp(const LaunchOptions& launchOptions)
        : phone(makePhoneLaunch(launchOptions))
        , root(makeRootTerminalConfig(phone), getRootPaneId(phone))
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
            [](const Graphics::SoftwareKeyboardChange& change)
        {
            LOG("software keyboard covers ",
                change.coveredHeight,
                " points, animating ",
                change.animationSeconds,
                " s");
        };

        if (launchOptions.raisesSoftwareKeyboard)
        {
            root.terminal.setWantsTextInput(true);
            root.terminal.focus();
        }

        if (launchOptions.turnsToLandscape)
            cowterm::mobile::requestLandscapeInterfaceOrientation(window);
    }

    std::unique_ptr<cowterm::mobile::PhoneLaunch> phone;
    MobileRootView root;
    Graphics::Window window {root, makeWindowOptions()};
};
} // namespace

int main(int argc, char* argv[])
{
    term::registerEmbeddedFonts();

    return Apps::run<CowTermMobileApp>(parseLaunchOptions(argc, argv));
}
