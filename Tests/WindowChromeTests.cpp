#include "TerminalViewTestPane.h"
#include "WindowChrome.h"
#include "Multiplexer/HostColours.h"
#include "Multiplexer/Projects.h"

namespace
{
constexpr auto remoteHost = "jamiepond@tamby";
constexpr auto remoteProject = "jamiepond@tamby:~/projects/tamber-web";
constexpr auto localSessionName = "CowTerm";
constexpr auto thisMachineName = "big-cow";
constexpr auto chromeWidth = 600.f;
constexpr auto pixelBesideTheLabel = 4;

const auto windowBounds = Graphics::Rect {0.f, 0.f, 1100.f, 700.f};

// The strip on its own, rendered off screen the way the window shows it.
struct RenderedChrome
{
    RenderedChrome()
    {
        host.setRootComponent(chrome);
        host.setBounds(
            calculateWindowChromeBounds(windowBounds.withWidth(chromeWidth)));
        host.resized();
    }

    Graphics::Color renderPixelBesideTheLabel()
    {
        const auto image = host.renderToImage(1.f);

        if (image.width() == 0)
            return {};

        return image.at(pixelBesideTheLabel, (int) (host.getBounds().h / 2.f));
    }

    bool canRender() { return host.renderToImage(1.f).width() != 0; }

    AppConfig config;
    WindowChrome chrome {config, getColourBlindSafeHostRowColours(config, thisMachineName)};
    UI::ComponentHost host;
};
} // namespace

auto tLocalChrome =
    test("WindowChrome/aLocalSessionShowsItsNameOnThisMachinesColour") = []
{
    const auto config = AppConfig {};
    const auto thisMachineColours = getColourBlindSafeHostRowColours(config, thisMachineName);
    const auto look = describeWindowChrome(config, thisMachineColours, "", localSessionName);

    check(look.label == localSessionName);
    check(look.background == thisMachineColours.background,
          "the colour the phone gives this Mac, which it dials as big-cow");
    check(look.text == thisMachineColours.text);
};

auto tRemoteChrome =
    test("WindowChrome/aRemoteSessionShowsItsHostAndNameOnTheHostsColour") = []
{
    const auto config = AppConfig {};
    const auto look = describeWindowChrome(
        config, getColourBlindSafeHostRowColours(config, thisMachineName), remoteHost,
        getSessionNameForProject(remoteProject));
    const auto hostColours = getColourBlindSafeHostRowColours(config, remoteHost);

    check(look.label == "tamby/tamber-web",
          "the host without its user, then the project");
    check(look.background == hostColours.background);
    check(look.text == hostColours.text);
};

auto tChromeLayout = test("WindowChrome/theStripAndTheSessionAreaTileTheWindow") = []
{
    const auto strip = calculateWindowChromeBounds(windowBounds);
    const auto session = calculateAreaUnderWindowChrome(windowBounds);

    check(strip.y == 0.f);
    check(strip.h > 0.f);
    check(strip.w == windowBounds.w);
    check(session.y == strip.h);
    check(session.w == windowBounds.w);
    check(session.h + strip.h == windowBounds.h);
};

auto tChromeFollowsTheSession =
    test("WindowChrome/theStripRepaintsForEachSessionShown") = []
{
    auto rendered = RenderedChrome {};

    if (!rendered.canRender())
        return;

    const auto hostColours =
        getColourBlindSafeHostRowColours(rendered.config, remoteHost);
    const auto thisMachineColours =
        getColourBlindSafeHostRowColours(rendered.config, thisMachineName);

    rendered.chrome.showSession(remoteHost, getSessionNameForProject(remoteProject));
    check(matches(rendered.renderPixelBesideTheLabel(), hostColours.background));

    rendered.chrome.showSession("", localSessionName);
    check(matches(rendered.renderPixelBesideTheLabel(), thisMachineColours.background));

    rendered.chrome.showSession(remoteHost, getSessionNameForProject(remoteProject));
    check(matches(rendered.renderPixelBesideTheLabel(), hostColours.background));
};
