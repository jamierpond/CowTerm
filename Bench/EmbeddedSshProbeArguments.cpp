#include "EmbeddedSshProbeArguments.h"

namespace
{
constexpr auto adoptFlag = "--adopt";
constexpr auto mostArguments = std::size_t {3};
} // namespace

std::optional<ProbeArguments> parseProbeArguments(const std::vector<std::string>& arguments)
{
    if (arguments.empty() || arguments.size() > mostArguments)
        return std::nullopt;

    const auto destination = term::parseSshDestination(arguments[0]);

    if (!destination)
        return std::nullopt;

    auto parsed = ProbeArguments {*destination};

    if (arguments.size() == 1)
        return parsed;

    if (arguments[1] != adoptFlag)
        return std::nullopt;

    parsed.isAdoptingAPane = true;

    if (arguments.size() == mostArguments)
        parsed.adoptedPaneId = arguments[2];

    return parsed;
}
