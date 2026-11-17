#include "SampleShell.h"

#include <eacp/Core/Threads/Timer.h>
#include <eacp/Core/Utils/Logging.h>

#include <optional>
#include <string_view>

namespace cowterm::mobile
{
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

// Claude Code asking to run a command, as its permission prompt draws.
constexpr std::string_view sampleClaudePromptText =
    "\r\n"
    "\xe2\x95\xad\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae\r\n"
    "\xe2\x94\x82 Bash command\r\n"
    "\xe2\x94\x82   rm -rf build\r\n"
    "\xe2\x94\x82   Remove the build folder\r\n"
    "\xe2\x94\x82 Do you want to proceed?\r\n"
    "\xe2\x94\x82 \xe2\x9d\xaf 1. Yes\r\n"
    "\xe2\x94\x82   2. Yes, and don't ask again for rm commands\r\n"
    "\xe2\x94\x82   3. No, and tell Claude what to do differently (esc)\r\n"
    "\xe2\x95\xb0\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x95\xaf\r\n";

constexpr auto streamedLinesPerSecond = 10;

// Stands in for the daemon until the ssh link exists: the pane shows
// sampleScreenText (and Claude's prompt when asked), then, when asked to
// stream, a numbered line ten times a second, and logs what is typed into it.
class SampleTextShell final : public term::Shell
{
public:
    explicit SampleTextShell(SampleShellOptions optionsToUse)
        : options(std::move(optionsToUse))
    {
    }

    bool start(const term::PtyOptions&,
               std::function<void(std::string)> onOutputToUse,
               std::function<void()>) override
    {
        onOutput = std::move(onOutputToUse);
        onOutput(std::string {sampleScreenText});

        if (options.showsClaudePrompt)
            onOutput(std::string {sampleClaudePromptText});

        onStatusChanged(options.statusNote);

        if (options.streamsOutput)
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

    SampleShellOptions options;
    int streamedLineCount = 0;
    std::function<void(std::string)> onOutput = [](std::string) {};
    std::optional<Threads::Timer> streamTimer;
};

} // namespace

term::TerminalConfig makeSampleShellConfig(const SampleShellOptions& options)
{
    auto config = term::TerminalConfig {};
    config.makeShell = [options](const std::string&)
    { return std::make_unique<SampleTextShell>(options); };
    return config;
}
} // namespace cowterm::mobile
