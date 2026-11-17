#include "PaneChoice.h"

#include "Multiplexer/Protocol.h"

#include <algorithm>
#include <charconv>

namespace term::phone
{
std::optional<LastInput> parseLastInput(const std::string& host,
                                        const std::string& message)
{
    constexpr auto timeArgument = std::size_t {1};
    const auto parsed = proto::parse(message);

    if (parsed.verb != "last-input" || parsed.args.size() <= timeArgument)
        return std::nullopt;

    const auto& timeText = parsed.args[timeArgument];
    auto answer = LastInput {{host, parsed.args[0]}, 0};
    const auto [end, error] = std::from_chars(timeText.data(),
                                              timeText.data() + timeText.size(),
                                              answer.millisecondsSinceEpoch);

    if (error != std::errc {})
        return std::nullopt;

    return answer;
}

std::optional<PaneOnHostId>
    chooseLaunchPane(const std::vector<LastInput>& lastInputs,
                     const std::optional<PaneOnHostId>& shownLastTime,
                     const IsPaneListed& isListed)
{
    if (!lastInputs.empty())
        return std::ranges::max_element(
                   lastInputs, {}, &LastInput::millisecondsSinceEpoch)
            ->pane;

    if (shownLastTime && isListed(*shownLastTime))
        return shownLastTime;

    return std::nullopt;
}

std::vector<std::string> listPanesOfSession(const std::vector<ListedPane>& listedPanes,
                                            const std::string& sessionName)
{
    auto ids = std::vector<std::string> {};

    for (const auto& pane: listedPanes)
    {
        if (pane.sessionName == sessionName)
            ids.push_back(pane.id);
    }

    return ids;
}
} // namespace term::phone
