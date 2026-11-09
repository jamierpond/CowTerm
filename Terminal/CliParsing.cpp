#include "CliParsing.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <cstdlib>
#include <sstream>

namespace term::cli
{
namespace
{
std::string jsonEscape(const std::string& text)
{
    auto out = std::string {};
    for (auto c: text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

long long readStateNumber(const std::string& text, const std::string& key)
{
    const auto label = "\"" + key + "\":";
    const auto at = text.find(label);

    if (at == std::string::npos)
        return 0;

    return std::atoll(text.c_str() + at + label.size());
}

std::string readStateString(const std::string& text, const std::string& key)
{
    const auto label = "\"" + key + "\":\"";
    const auto at = text.find(label);
    auto value = std::string {};

    if (at == std::string::npos)
        return value;

    for (auto i = at + label.size(); i < text.size() && text[i] != '"'; ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size())
            ++i;

        value += text[i];
    }

    return value;
}

bool isNotifyUserFlag(const std::string& arg)
{
    return arg.starts_with("--");
}

bool setCaptureFlag(Options& options, const std::string& name)
{
    static constexpr auto flags = std::array {
        std::pair {std::string_view {"--interactive"}, &Options::interactive},
        std::pair {std::string_view {"--audio"}, &Options::audio},
    };

    const auto found = std::ranges::find(flags, name, &decltype(flags)::value_type::first);

    if (found == flags.end())
        return false;

    options.*(found->second) = true;
    return true;
}

void setCaptureValue(Options& options, const std::string& name, const std::string& value)
{
    static constexpr auto values = std::array {
        std::pair {std::string_view {"-o"}, &Options::output},
        std::pair {std::string_view {"--display"}, &Options::display},
        std::pair {std::string_view {"--window"}, &Options::window},
        std::pair {std::string_view {"--rect"}, &Options::rect},
        std::pair {std::string_view {"--duration"}, &Options::duration},
    };

    const auto found = std::ranges::find(values, name, &decltype(values)::value_type::first);

    if (found != values.end())
        options.*(found->second) = value;
}

std::string readNotifyUserOptions(const std::vector<std::string>& args,
                                  std::size_t& index,
                                  NotifyUserRequest& request)
{
    while (index + 1 < args.size() && !isNotifyUserFlag(args[index + 1]))
        request.options.push_back(args[++index]);

    if (request.options.empty())
        return "--options needs at least one option";

    return {};
}

std::optional<std::string NotifyUserRequest::*> findNotifyUserValueField(const std::string& name)
{
    static constexpr auto fields = std::array {
        std::pair {std::string_view {"--content"}, &NotifyUserRequest::content},
        std::pair {std::string_view {"--title"}, &NotifyUserRequest::title},
        std::pair {std::string_view {"--pane"}, &NotifyUserRequest::paneId},
    };

    const auto found = std::ranges::find(fields, name, &decltype(fields)::value_type::first);

    if (found == fields.end())
        return std::nullopt;

    return found->second;
}

// Reads the argument at index (and any values it takes, advancing index past
// them) into request; returns the usage error, or nothing.
std::string readNotifyUserArgument(const std::vector<std::string>& args,
                                   std::size_t& index,
                                   NotifyUserRequest& request,
                                   bool& hasContent)
{
    const auto& arg = args[index];

    if (arg == "--allow-type-response")
    {
        request.allowsTypedResponse = true;
        return {};
    }

    if (arg == "--options")
        return readNotifyUserOptions(args, index, request);

    const auto field = findNotifyUserValueField(arg);

    if (!field)
        return "unknown option: " + arg;

    if (index + 1 >= args.size())
        return arg + " needs a value";

    request.*(*field) = args[++index];
    hasContent = hasContent || arg == "--content";
    return {};
}
} // namespace

ParsedOptions parseCaptureOptions(const std::vector<std::string>& args,
                                  const std::vector<std::string>& allowed)
{
    auto options = Options {};

    for (auto i = std::size_t {0}; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (std::ranges::find(allowed, arg) == allowed.end())
            return {std::nullopt, "unknown option: " + arg + " (see ct help)"};

        if (setCaptureFlag(options, arg))
            continue;

        if (i + 1 >= args.size())
            return {std::nullopt, arg + " needs a value"};

        setCaptureValue(options, arg, args[++i]);
    }

    return {options, {}};
}

ParsedNotifyUserRequest parseNotifyUserArguments(const std::vector<std::string>& args,
                                                 const std::string& defaultPaneId)
{
    auto request = NotifyUserRequest {};
    request.paneId = defaultPaneId;
    auto hasContent = false;

    for (auto i = std::size_t {0}; i < args.size(); ++i)
    {
        auto error = readNotifyUserArgument(args, i, request, hasContent);

        if (!error.empty())
            return {std::nullopt, std::move(error)};
    }

    if (!hasContent)
        return {std::nullopt, "--content is required"};

    if (request.paneId.empty())
        return {std::nullopt, "no pane: run it inside a CowTerm pane or pass --pane <id>"};

    return {request, {}};
}

std::string formatRecordingState(const Recording& recording)
{
    auto stream = std::ostringstream {};
    stream << "{\"pid\":" << recording.pid << ",\"path\":\""
           << jsonEscape(recording.path) << "\",\"started\":" << recording.started
           << "}\n";
    return stream.str();
}

std::optional<Recording> parseRecordingState(const std::string& text)
{
    auto recording = Recording {};
    recording.pid = readStateNumber(text, "pid");
    recording.started = readStateNumber(text, "started");
    recording.path = readStateString(text, "path");

    if (recording.pid <= 0 || recording.path.empty())
        return std::nullopt;

    return recording;
}
} // namespace term::cli
