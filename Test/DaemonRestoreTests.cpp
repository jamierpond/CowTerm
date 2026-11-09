#include <CowTermCore/TermState.h>
#include <Protocol.h>

#include <NanoTest/NanoTest.h>
#include <eacp/Core/Core.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Network/IPC/Messenger.h>

#include <optional>
#include <string>
#include <chrono>

using namespace nano;
using namespace eacp;

namespace
{
constexpr auto messageTimeout = Time::MS {15000};
constexpr auto paneId = "restore-test-pane";
constexpr auto marker = "restore-me-42";

std::string makeInstanceName()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "daemon-restore-test-" + std::to_string(now);
}

void launchDaemon(const std::string& instance)
{
    auto options = Processes::ProcessOptions {};
    options.executable = COWTERM_DAEMON_PATH;
    options.environment.push_back({"COWTERM_INSTANCE", instance});
    options.detached = true;
    auto daemon = Processes::Process {options};
}

// One conversation with a daemon instance: collects every message it sends.
struct DaemonConversation
{
    explicit DaemonConversation(const std::string& instance)
        : messenger("cowtermd-" + instance)
    {
        messenger.onConnected = [this] { connected = true; };
        messenger.onDisconnected = [this] { disconnected = true; };
        messenger.onMessage = [this](const std::string& body)
        { received.push_back(term::proto::parse(body)); };
    }

    bool waitForConnection()
    {
        return Threads::runEventLoopUntil([this] { return connected; }, messageTimeout);
    }

    bool waitForOutputContaining(const std::string& text)
    {
        return Threads::runEventLoopUntil(
            [&]
            {
                auto output = std::string {};

                for (const auto& message: received)
                {
                    if (message.verb == "output")
                        output += message.payload;
                }

                return output.find(text) != std::string::npos;
            },
            messageTimeout);
    }

    std::optional<term::TerminalState> waitForState()
    {
        auto state = std::optional<term::TerminalState> {};

        Threads::runEventLoopUntil(
            [&]
            {
                for (const auto& message: received)
                {
                    if (message.verb == "state")
                        state = term::parseTerminalState(message.payload);
                }

                return state.has_value();
            },
            messageTimeout);

        return state;
    }

    void quitDaemon()
    {
        messenger.send(term::proto::make("quit-server"));
        Threads::runEventLoopUntil([this] { return disconnected; }, messageTimeout);
    }

    IPC::Messenger messenger;
    std::vector<term::proto::Message> received;
    bool connected = false;
    bool disconnected = false;
};

bool hasLineContaining(const term::ScreenState& screen, const std::string& text)
{
    for (const auto* lines: {&screen.scrollback, &screen.primary})
    {
        for (const auto& line: *lines)
        {
            auto lineText = std::string {};

            for (const auto& run: line.runs)
                lineText += run.text;

            if (lineText.find(text) != std::string::npos)
                return true;
        }
    }

    return false;
}
} // namespace

auto tPaneComesBackAfterDaemonRestart = test("DaemonRestore/paneComesBackAfterDaemonRestart") = []
{
    const auto instance = makeInstanceName();

    {
        launchDaemon(instance);
        auto first = DaemonConversation {instance};
        check(first.waitForConnection());

        first.messenger.send(term::proto::make("spawn", {paneId, "80", "24"}, "/tmp"));
        first.messenger.send(
            term::proto::make("write", {paneId}, std::string {"echo "} + marker + "\n"));
        check(first.waitForOutputContaining(std::string {marker} + "\r\n"));
        first.quitDaemon();
    }

    {
        launchDaemon(instance);
        auto second = DaemonConversation {instance};
        check(second.waitForConnection());

        second.messenger.send(term::proto::make("spawn", {paneId, "80", "24"}, "/tmp"));
        const auto state = second.waitForState();
        check(state.has_value());

        if (state)
        {
            check(hasLineContaining(state->screen, marker));
            check(!state->screen.modes.altScreen);
        }

        second.messenger.send(term::proto::make("kill", {paneId}));
        second.quitDaemon();
    }

    Files::removeAll(FilePath::appDataDirectory() / "tamber" / ("cowterm-panes-" + instance));
};
