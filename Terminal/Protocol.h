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
