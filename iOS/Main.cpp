#include "TerminalView/GlyphAtlas.h"
#include "TerminalView/TerminalView.h"

#include <eacp/Graphics/Window/ViewWindow.h>

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
// sampleScreenText once and ignores everything typed into it.
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

    void write(std::string_view) override {}
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
} // namespace

int main()
{
    term::registerEmbeddedFonts();

    return Graphics::runWindowedApp<term::TerminalView>(
        Graphics::WindowOptions {}, makeSampleTextTerminalConfig(), std::string {});
}
