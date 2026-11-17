#include "OverviewLayout.h"

#include "Multiplexer/SessionOverviewSearch.h"
#include "Phone/StoredScreen.h"

#include <algorithm>
#include <numeric>

namespace cowterm::mobile
{
using eacp::Graphics::Point;
using eacp::Graphics::Rect;
using term::overview::OverviewRow;

namespace
{
constexpr auto waitingCardHeight = 84.0f;
constexpr auto sessionRowHeight = 56.0f;
constexpr auto unreachableHostRowHeight = 56.0f;
constexpr auto itemGap = 6.0f;
constexpr auto everyMachineUnreachableBanner = "No machine answers.\nCheck the network or Tailscale.";
constexpr auto noHostsSetUpBanner = "No hosts set up.\nRun just ios-pair on the Mac.";

struct ItemHeight
{
    OverviewItemKind kind;
    float height;
};

constexpr ItemHeight itemHeights[] = {
    {OverviewItemKind::WaitingCard, waitingCardHeight},
    {OverviewItemKind::SessionRow, sessionRowHeight},
    {OverviewItemKind::UnreachableHostRow, unreachableHostRowHeight},
};

bool isEveryMachineUnreachable(const OverviewInputs& inputs)
{
    const auto unreachableCount = static_cast<int>(inputs.unreachableHosts.size());
    return unreachableCount >= inputs.hostCount;
}

std::optional<std::int64_t> findLastUseMilliseconds(const OverviewRow& session, const OverviewInputs& inputs)
{
    if (session.lastUsedMilliseconds > 0)
        return session.lastUsedMilliseconds;

    return inputs.lastOpeningOnPhone(term::overview::makeOverviewRowKey(session));
}

std::string describeLastUse(const OverviewRow& session, const OverviewInputs& inputs)
{
    const auto lastUse = findLastUseMilliseconds(session, inputs);

    if (!lastUse)
        return {};

    return term::phone::describeLastUseAge(inputs.nowMilliseconds - *lastUse);
}

OverviewItem makeSessionItem(OverviewItemKind kind, const OverviewRow& session, const OverviewInputs& inputs)
{
    auto item = OverviewItem {};
    item.kind = kind;
    item.session = session;
    item.lastUse = describeLastUse(session, inputs);
    return item;
}

OverviewItem makeUnreachableItem(const UnreachableHost& host)
{
    auto item = OverviewItem {};
    item.kind = OverviewItemKind::UnreachableHostRow;
    item.unreachable = host;
    return item;
}

float addItemHeightAndGap(float total, float height)
{
    return total + height + itemGap;
}

std::vector<OverviewItem> listSearchResults(const OverviewInputs& inputs)
{
    const auto matches = term::overview::searchOverviewRows(
        inputs.sessions, inputs.query, inputs.nowMilliseconds, inputs.openingUsage);
    auto items = std::vector<OverviewItem> {};

    for (const auto& session: matches)
        items.push_back(makeSessionItem(OverviewItemKind::SessionRow, session, inputs));

    return items;
}

OverviewItemKind chooseSessionItemKind(const OverviewRow& session)
{
    if (session.isWaiting)
        return OverviewItemKind::WaitingCard;

    return OverviewItemKind::SessionRow;
}

std::vector<OverviewItem> listWholeOverview(const OverviewInputs& inputs)
{
    auto items = std::vector<OverviewItem> {};
    const auto ranked =
        term::overview::searchOverviewRows(inputs.sessions, {}, inputs.nowMilliseconds, inputs.openingUsage);

    for (const auto& session: ranked)
        items.push_back(makeSessionItem(chooseSessionItemKind(session), session, inputs));

    std::stable_partition(items.begin(),
                          items.end(),
                          [](const OverviewItem& item)
                          { return item.kind == OverviewItemKind::WaitingCard; });

    for (const auto& host: inputs.unreachableHosts)
        items.push_back(makeUnreachableItem(host));

    return items;
}
} // namespace

OverviewContent arrangeOverview(const OverviewInputs& inputs)
{
    auto content = OverviewContent {};

    if (inputs.hostCount == 0)
        content.banner = noHostsSetUpBanner;
    else if (isEveryMachineUnreachable(inputs))
        content.banner = everyMachineUnreachableBanner;
    else if (!inputs.query.empty())
        content.itemsFromTheBottom = listSearchResults(inputs);
    else
        content.itemsFromTheBottom = listWholeOverview(inputs);

    return content;
}

OverviewAreas calculateOverviewAreas(Rect screen, eacp::Graphics::Insets safeArea, float keyboardCoveredHeight)
{
    constexpr auto sidePadding = 12.0f;
    constexpr auto searchFieldHeight = 44.0f;
    constexpr auto searchFieldGap = 8.0f;
    constexpr auto hideKeyboardButtonWidth = 64.0f;

    const auto isKeyboardUp = keyboardCoveredHeight > 0.0f;
    auto area = screen;
    area.removeFromTop(safeArea.top);
    area.removeFromBottom(std::max(safeArea.bottom, keyboardCoveredHeight) + searchFieldGap);
    area = area.inset(sidePadding, 0.0f);

    auto areas = OverviewAreas {};
    areas.searchField = area.removeFromBottom(searchFieldHeight);

    if (isKeyboardUp)
    {
        areas.hideKeyboardButton = areas.searchField.removeFromRight(hideKeyboardButtonWidth);
        areas.searchField.removeFromRight(searchFieldGap);
    }

    area.removeFromBottom(searchFieldGap);
    areas.list = area;
    return areas;
}

float getOverviewItemHeight(OverviewItemKind kind)
{
    for (const auto& entry: itemHeights)
    {
        if (entry.kind == kind)
            return entry.height;
    }

    return sessionRowHeight;
}

std::vector<float> listOverviewItemHeights(const std::vector<OverviewItem>& items)
{
    auto heights = std::vector<float> {};

    for (const auto& item: items)
        heights.push_back(getOverviewItemHeight(item.kind));

    return heights;
}

float calculateMaximumOverviewScroll(const std::vector<float>& heights, float listHeight)
{
    const auto contentHeight =
        std::accumulate(heights.begin(), heights.end(), 0.0f, addItemHeightAndGap);
    return std::max(0.0f, contentHeight - listHeight);
}

std::vector<Rect> layOutFromTheBottom(const std::vector<float>& heights, Rect list, float scrollOffset)
{
    auto bounds = std::vector<Rect> {};
    auto bottom = list.y + list.h + scrollOffset;

    for (const auto height: heights)
    {
        bottom -= height;
        bounds.push_back({list.x, bottom, list.w, height});
        bottom -= itemGap;
    }

    return bounds;
}

std::optional<std::size_t> findOverviewItemAt(const std::vector<Rect>& itemBounds, Point position)
{
    for (auto index = std::size_t {0}; index < itemBounds.size(); ++index)
    {
        if (itemBounds[index].contains(position))
            return index;
    }

    return std::nullopt;
}
} // namespace cowterm::mobile
