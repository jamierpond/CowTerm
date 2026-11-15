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
bool isNotifyUserFlag(const std::string& arg)
{
    return arg.starts_with("--");
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

} // namespace term::cli
