// Not a test: renders Ctrl+A F off screen to a PNG, with this machine's
// config, so a change to how the picker looks can be seen without a window.
// Remote hosts in searchDirs are connected first, so their rows are real.
// Run it under an isolated instance (COWTERM_INSTANCE) by scripts/render-picker.sh.
// usage: CowTermRenderPicker <out.png> [query] [rows to move the selection down]

#include "Palette.h"
#include "Multiplexer/RemoteHosts.h"
#include "Multiplexer/SshBridgeLink.h"
#include "Session.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/UI/UI.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace eacp;

namespace
{
constexpr auto pictureWidth = 1100.0f;
constexpr auto pictureHeight = 760.0f;
constexpr auto pictureScale = 2.0f;
constexpr auto waitForHosts = Time::MS {20000};
constexpr std::uint16_t downArrowKeyCode = 125;

bool haveAllHostsAnswered()
{
    for (auto* host: term::RemoteHosts::all())
    {
        const auto answered = host->getStatus() == term::RemoteHost::Status::Unreachable
                              || !host->getLastScan().empty();

        if (!answered)
            return false;
    }

    return true;
}

void typeQuery(term::Palette& palette, const std::string& query)
{
    for (const auto character: query)
    {
        auto key = UI::KeyEvent {};
        key.characters = std::string(1, character);
        key.charactersIgnoringModifiers = key.characters;
        palette.keyDown(key);
    }
}

void moveSelectionDown(term::Palette& palette, int rows)
{
    auto down = UI::KeyEvent {};
    down.keyCode = downArrowKeyCode;

    for (auto row = 0; row < rows; ++row)
        palette.keyDown(down);
}

int renderPicker(const std::string& outputPath, const std::string& query, int selectedRow)
{
    const auto config = term::loadConfig();
    term::RemoteHosts::configure(config, term::makeSshBridgeLink);
    Threads::runEventLoopUntil(haveAllHostsAnswered, waitForHosts);

    auto sessions = term::SessionManager {config};
    const auto noUnreadPanes = term::UnreadReports {};
    auto palette = term::Palette {config, sessions, noUnreadPanes};
    auto host = UI::ComponentHost {};
    host.setRootComponent(palette);
    host.setBounds({0.0f, 0.0f, pictureWidth, pictureHeight});
    host.resized();

    palette.show();
    typeQuery(palette, query);
    moveSelectionDown(palette, selectedRow);

    const auto image = host.renderToImage(pictureScale);
    term::RemoteHosts::teardown();

    if (image.width() == 0)
    {
        std::fprintf(stderr, "no GPU device: nothing rendered\n");
        return 1;
    }

    image.save(FilePath {outputPath});
    std::printf("%s\n", outputPath.c_str());
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: CowTermRenderPicker <out.png> [query] [selected row]\n");
        return 2;
    }

    const auto outputPath = std::string {argv[1]};
    const auto query = argc > 2 ? std::string {argv[2]} : std::string {};
    const auto selectedRow = argc > 3 ? eacp::Strings::parseIntOr(argv[3]) : 0;
    return Apps::run([&] { Apps::quit(renderPicker(outputPath, query, selectedRow)); });
}
