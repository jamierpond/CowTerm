#pragma once

// A real session daemon under its own instance name, and one conversation
// with it, for the tests that drive the daemon over IPC.

#include <Emulator/TermState.h>
#include <Multiplexer/Protocol.h>

#include <eacp/Core/Core.h>
#include <eacp/Network/IPC/Messenger.h>

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace daemontest
{
using namespace eacp;

constexpr auto messageTimeout = Time::MS {15000};

inline std::string makeInstanceName(const std::string& testName)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return testName + "-" + std::to_string(now);
}

// Not detached: the test waits for each daemon to exit before starting the
// next, so a relaunch never connects to the previous one while it shuts down.
inline Processes::Process launchDaemon(const std::string& instance)
{
    auto options = Processes::ProcessOptions {};
    options.executable = COWTERM_DAEMON_PATH;
    options.environment.push_back({"COWTERM_INSTANCE", instance});
    return Processes::Process {options};
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

    // The next message with this verb that no earlier call returned.
    std::optional<term::proto::Message> waitForMessage(const std::string& verb)
    {
        auto found = std::optional<term::proto::Message> {};

        Threads::runEventLoopUntil(
            [&]
            {
                auto& next = nextUnreadMessageByVerb[verb];

                for (; next < received.size(); ++next)
                {
                    if (received[next].verb == verb)
                    {
                        found = received[next++];
                        return true;
                    }
                }

                return false;
            },
            messageTimeout);

        return found;
    }

    std::string listReceivedVerbs() const
    {
        auto verbs = std::string {"received:"};

        for (const auto& message: received)
            verbs += " " + message.verb;

        return verbs;
    }

    void quitDaemon()
    {
        messenger.send(term::proto::make("quit-server"));
        Threads::runEventLoopUntil([this] { return disconnected; }, messageTimeout);
    }

    IPC::Messenger messenger;
    std::vector<term::proto::Message> received;
    std::map<std::string, std::size_t> nextUnreadMessageByVerb;
    bool connected = false;
    bool disconnected = false;
};

inline bool hasLineContaining(const term::ScreenState& screen, const std::string& text)
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
} // namespace daemontest
