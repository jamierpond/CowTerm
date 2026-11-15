#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace term::proto
{
// A named instance ($COWTERM_INSTANCE) keeps its own daemon, config and store
// so a second copy can run fully isolated from the user's live session — used
// by the test/debug harness. Empty (the default) is the normal instance.
inline std::string instanceSuffix()
{
    const auto* name = std::getenv("COWTERM_INSTANCE");
    return name != nullptr ? std::string {name} : std::string {};
}

// The IPC name the daemon serves and the app dials.
inline std::string serverName()
{
    const auto suffix = instanceSuffix();
    return suffix.empty() ? std::string {"cowtermd"} : "cowtermd-" + suffix;
}

// The IPC name the running app serves and `ct notify-user` dials.
inline std::string appServerName()
{
    const auto suffix = instanceSuffix();
    return suffix.empty() ? std::string {"cowterm"} : "cowterm-" + suffix;
}

// What the daemon answers to "hello". Bumped when the daemon's messages
// change, so a newer `ct bridge` restarts an older daemon (its panes come back
// through the saved-state restore) instead of talking past it.
inline constexpr auto daemonProtocolVersion = 4;

// spawn's payload: the working directory, then optionally a newline and a
// command to run in place of an interactive shell.
struct SpawnPayload
{
    std::string workingDirectory;
    std::string command;
};

inline std::string makeSpawnPayload(const SpawnPayload& spawn)
{
    if (spawn.command.empty())
        return spawn.workingDirectory;

    return spawn.workingDirectory + "\n" + spawn.command;
}

inline SpawnPayload parseSpawnPayload(const std::string& payload)
{
    const auto newline = payload.find('\n');

    if (newline == std::string::npos)
        return {payload, {}};

    return {payload.substr(0, newline), payload.substr(newline + 1)};
}

// Set in every pane's shell environment to the pane's stable shell id, so
// `ct notify-user` run there knows which pane asked.
inline constexpr auto paneEnvironmentVariable = "COWTERM_PANE";

// One Messenger message: a verb line ("verb arg1 arg2..."), then an
// optional raw payload after the first newline. The payload is untouched
// bytes — Messenger frames are length-prefixed, so PTY output needs no
// escaping.
struct Message
{
    std::string verb;
    std::vector<std::string> args;
    std::string payload;
};

inline Message parse(const std::string& raw)
{
    auto message = Message {};
    const auto lineEnd = raw.find('\n');
    const auto line = raw.substr(0, lineEnd);

    if (lineEnd != std::string::npos)
        message.payload = raw.substr(lineEnd + 1);

    auto start = std::size_t {0};

    while (start <= line.size())
    {
        const auto end = std::min(line.find(' ', start), line.size());

        if (end > start)
        {
            auto word = line.substr(start, end - start);

            if (message.verb.empty())
                message.verb = std::move(word);
            else
                message.args.push_back(std::move(word));
        }

        start = end + 1;
    }

    return message;
}

// Arguments are split on spaces, so one that may hold spaces (a session
// name) travels with ' ' and '%' percent-encoded.
inline std::string encodeArgument(std::string_view text)
{
    auto encoded = std::string {};

    for (const auto character: text)
    {
        if (character == ' ')
            encoded += "%20";
        else if (character == '%')
            encoded += "%25";
        else
            encoded += character;
    }

    return encoded;
}

inline std::string decodeArgument(std::string_view text)
{
    auto decoded = std::string {};

    for (auto index = std::size_t {0}; index < text.size(); ++index)
    {
        const auto escape = text.substr(index, 3);

        if (escape == "%20" || escape == "%25")
        {
            decoded += escape == "%20" ? ' ' : '%';
            index += 2;
        }
        else
        {
            decoded += text[index];
        }
    }

    return decoded;
}

inline std::string make(std::string_view verb,
                        std::initializer_list<std::string_view> args = {},
                        std::string_view payload = {})
{
    auto message = std::string {verb};

    for (const auto& arg: args)
    {
        message += ' ';
        message += arg;
    }

    if (!payload.empty())
    {
        message += '\n';
        message += payload;
    }

    return message;
}
} // namespace term::proto
