// Not a test: renders the palette, the switcher and each session's panes under
// the window chrome strip off screen to PNGs, with sessions on Jamie's hosts
// and a local one, so the host colours can be looked at without a window. This
// Mac is named big-cow, as Jamie's cowterm.json names it, so its local session
// should take big-cow's colour everywhere. The
// session on tamby is marked unread (finished) and the one on macprorack
// (waiting), so the unread pills and their order can be looked at too. The
// shells are scripted: no daemon, no ssh, no real program runs. Run it under an isolated instance
// (COWTERM_INSTANCE) by scripts/render-host-colours.sh. Given a cowterm.json,
// its host colours apply; without one every host takes its default colour.
// usage: CowTermRenderHostColours <output directory> [cowterm.json]

#include "PaneTerminalConfig.h"
#include "Palette.h"
#include "Session.h"
#include "Switcher.h"
#include "WindowChrome.h"
#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/UI/UI.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace eacp;

namespace
{
constexpr auto pictureScale = 1.0f;
constexpr auto waitForOutput = Time::MS {4000};
constexpr std::uint16_t downArrowKeyCode = 125;
constexpr auto onIPhoneNote = "on iPhone, 49 columns; click here to take it back";
constexpr auto sessionWithTheNote = "jamiepond@tamby";
constexpr auto unreadSinceMilliseconds = std::int64_t {1'800'000'000'000};
constexpr auto thisMachineName = "big-cow";

struct UnreadSession
{
    std::string host;
    std::string event;
    std::int64_t sinceMilliseconds = 0;
};

const auto unreadSessions = std::array {UnreadSession {sessionWithTheNote, "finished", unreadSinceMilliseconds},
                                        UnreadSession {"jamiepond@macprorack", "waiting", unreadSinceMilliseconds - 1}};

struct WindowShape
{
    std::string name;
    float width = 0.0f;
    float height = 0.0f;
};

const auto windowShapes = std::array {WindowShape {"minimum", 300.0f, 200.0f},
                                      WindowShape {"tall-narrow", 420.0f, 1000.0f},
                                      WindowShape {"default", 1100.0f, 700.0f},
                                      WindowShape {"ultrawide", 2560.0f, 900.0f}};

const auto projectsToOpen = std::array {std::string {"/tmp/CowTerm"},
                                        std::string {"jamiepond@big-cow:~/projects/DiffView"},
                                        std::string {"jamiepond@tamby:~/projects/tamber-web"},
                                        std::string {"jamiepond@macprorack:~/projects/eacp"},
                                        std::string {"jamie@tamby-windows:~/projects/Cows"}};

// Prints a prompt and a listing once, then sits still; a pane on tamby also
// says the phone holds it, as the daemon would.
class ScriptedShell final : public term::Shell
{
public:
    explicit ScriptedShell(std::string hostToUse)
        : host(std::move(hostToUse))
    {
    }

    bool start(const term::PtyOptions&, std::function<void(std::string)> onOutput, std::function<void()>) override
    {
        onOutput(describePrompt() + "ls\r\nREADME.md  Lib  Tests  docs\r\n" + describePrompt());

        if (host == sessionWithTheNote)
            onStatusChanged(onIPhoneNote);

        return true;
    }

    void write(std::string_view) override {}
    void resize(const term::PtySize&) override {}
    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }
    void terminate() override {}
    void detach() override {}

private:
    std::string describePrompt() const
    {
        if (host.empty())
            return "this-mac % ";

        return host + " % ";
    }

    std::string host;
};

// The app's window in miniature: the window chrome strip, and the active
// session's panes under it with an overlay over them.
class RenderWindow final : public Graphics::View
{
public:
    explicit RenderWindow(const term::AppConfig& config)
        : chrome(config, term::getColourBlindSafeHostRowColours(config, thisMachineName))
    {
        chromeHost.setRootComponent(chrome);
        addSubview(chromeHost);
    }

    void show(term::TermSession& session, Graphics::Rect bounds)
    {
        clear();
        shown = &session.view;
        addSubview(session.view);
        setBounds(bounds);
        chrome.showSession(session.host, session.name);
        chromeHost.setBounds(term::calculateWindowChromeBounds(bounds));
        chromeHost.resized();
        session.view.setBounds(term::calculateAreaUnderWindowChrome(bounds));
        layOutOffScreen(session.view);
    }

    void showOverlay(UI::Component& overlay)
    {
        overlayHost.setRootComponent(overlay);
        addSubview(overlayHost);
        overlayHost.setBounds(
            term::calculateAreaUnderWindowChrome(getLocalBounds()));
        overlayHost.resized();
    }

    void clear()
    {
        if (shown != nullptr)
            removeSubview(*shown);

        removeSubview(overlayHost);
        shown = nullptr;
    }

    // Off screen, setBounds alone never calls resized(), which lays out the
    // panes and builds their renderers.
    static void layOutOffScreen(term::SessionView& sessionView)
    {
        sessionView.resized();

        for (auto* pane: sessionView.panes())
            pane->resized();
    }

private:
    term::SessionView* shown = nullptr;
    term::WindowChrome chrome;
    UI::ComponentHost chromeHost;
    UI::ComponentHost overlayHost;
};

bool hasEveryPaneDrawnItsPrompt(const term::SessionManager& sessions)
{
    for (const auto& session: sessions.all())
    {
        for (auto* pane: session->view.panes())
        {
            if (pane->captureVisibleText().empty())
                return false;
        }
    }

    return true;
}

void saveImage(const Graphics::Image& image, const std::filesystem::path& path)
{
    image.save(FilePath {path.string()});
    std::printf("%s\n", path.string().c_str());
}

UI::KeyEvent makeKeyEvent(std::uint16_t keyCode)
{
    auto key = UI::KeyEvent {};
    key.keyCode = keyCode;
    return key;
}

// Runs what the overlays queued with callAsync while they are still alive.
void runQueuedCalls()
{
    auto reached = false;
    Threads::callAsync([&reached] { reached = true; });
    Threads::runEventLoopUntil([&reached] { return reached; }, waitForOutput);
}

std::string describeSessionForFileName(const term::TermSession& session)
{
    if (session.host.empty())
        return "local";

    return term::remote::getHostLabel(session.host);
}

term::TermSession& findSession(term::SessionManager& sessions, const std::string& host)
{
    for (const auto& session: sessions.all())
    {
        if (session->host == host)
            return *session;
    }

    return *sessions.all().front();
}

// An `activity` answer as the session's daemon would give it, with every pane
// of the session unread.
std::string makeUnreadActivityReport(const term::TermSession& session, const UnreadSession& unread)
{
    auto payload = std::string {};

    for (const auto& shellId: session.listShellIds())
    {
        payload += shellId + "\t0\t0\t" + unread.event + "\t0\t\t" + unread.event + "\t"
                   + std::to_string(unread.sinceMilliseconds) + "\tAll tests pass.\n";
    }

    return payload;
}

term::UnreadReports makeUnreadReports(term::SessionManager& sessions)
{
    auto reports = term::UnreadReports {};

    for (const auto& unread: unreadSessions)
        reports.takeActivityReport(unread.host, makeUnreadActivityReport(findSession(sessions, unread.host), unread));

    return reports;
}

void renderShape(term::SessionManager& sessions,
                 const term::AppConfig& config,
                 const WindowShape& shape,
                 const std::filesystem::path& directory)
{
    const auto bounds = Graphics::Rect {0.0f, 0.0f, shape.width, shape.height};
    auto window = RenderWindow {config};
    auto& tamby = findSession(sessions, sessionWithTheNote);

    for (const auto& session: sessions.all())
    {
        window.show(*session, bounds);
        const auto fileName = "session-" + describeSessionForFileName(*session) + "-" + shape.name + ".png";
        saveImage(window.renderToImage(pictureScale), directory / fileName);
    }

    window.show(tamby, bounds);
    const auto unread = makeUnreadReports(sessions);
    const auto thisMachineColours = term::getColourBlindSafeHostRowColours(config, thisMachineName);
    auto palette = term::Palette {config, thisMachineColours, sessions, unread};
    window.showOverlay(palette);
    palette.show();
    palette.keyDown(makeKeyEvent(downArrowKeyCode));
    saveImage(window.renderToImage(pictureScale), directory / ("palette-" + shape.name + ".png"));

    auto switcher = term::Switcher {config, thisMachineColours, sessions, unread};
    window.clear();
    window.show(tamby, bounds);
    switcher.begin(false);
    window.showOverlay(switcher);
    saveImage(window.renderToImage(pictureScale), directory / ("switcher-" + shape.name + ".png"));
    switcher.keyDown(makeKeyEvent(UI::KeyCode::Escape));
    runQueuedCalls();

    tamby.view.splitActive(true);
    window.clear();
    window.show(tamby, bounds);
    Threads::runEventLoopUntil([&sessions] { return hasEveryPaneDrawnItsPrompt(sessions); }, waitForOutput);
    saveImage(window.renderToImage(pictureScale), directory / ("session-split-" + shape.name + ".png"));
    tamby.view.closeActivePane();
    window.clear();
}

int renderHostColours(const std::filesystem::path& directory, const term::AppConfig& config)
{
    std::filesystem::create_directories(directory);

    auto sessions = term::SessionManager {config};

    for (const auto& project: projectsToOpen)
        sessions.newSession(project);

    Threads::runEventLoopUntil([&sessions] { return hasEveryPaneDrawnItsPrompt(sessions); }, waitForOutput);

    for (const auto& shape: windowShapes)
        renderShape(sessions, config, shape, directory);

    return 0;
}
} // namespace

namespace term
{
TerminalConfig terminalConfig(const AppConfig& config, const std::string& host, const std::string&)
{
    auto result = TerminalConfig {};
    result.font = config.font;
    result.fontSize = config.fontSize;
    result.theme = themeByName(config.theme);
    result.makeShell = [host](const std::string&) { return std::make_unique<ScriptedShell>(host); };
    return result;
}
} // namespace term

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: CowTermRenderHostColours <output directory> [cowterm.json]\n");
        return 2;
    }

    const auto directory = std::filesystem::path {argv[1]};
    const auto hasConfigFile = argc > 2;
    const auto config = hasConfigFile ? term::parseConfig(Files::readFile(argv[2])) : term::AppConfig {};
    return Apps::run([&] { Apps::quit(renderHostColours(directory, config)); });
}
