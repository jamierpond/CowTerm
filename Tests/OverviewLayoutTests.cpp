#include "OverviewLayout.h"

#include <NanoTest/NanoTest.h>

// The phone overview's arrangement and its bottom-anchored list, on the Mac.

using namespace nano;
using namespace cowterm::mobile;
using term::overview::OverviewRow;

namespace
{
constexpr auto now = std::int64_t {1'800'000'000'000};
constexpr auto oneMinute = std::int64_t {60'000};
constexpr auto laptop = "jamie@tamby";
constexpr auto rack = "jamiepond@macprorack";
constexpr auto listTop = 100.0f;
constexpr auto listHeight = 400.0f;
constexpr auto listWidth = 360.0f;
constexpr auto rowHeight = 50.0f;
constexpr auto tolerance = 1.0e-3f;

const auto list = eacp::Graphics::Rect {0.0f, listTop, listWidth, listHeight};

OverviewRow makeSession(const std::string& name, bool isWaiting)
{
    auto row = OverviewRow {};
    row.host = laptop;
    row.sessionName = name;
    row.isWaiting = isWaiting;
    row.lastUsedMilliseconds = now - oneMinute;
    return row;
}

OverviewInputs makeInputs(std::vector<OverviewRow> sessions)
{
    auto inputs = OverviewInputs {};
    inputs.sessions = std::move(sessions);
    inputs.hostCount = 1;
    inputs.nowMilliseconds = now;
    return inputs;
}

std::vector<OverviewItemKind> listKinds(const OverviewContent& content)
{
    auto kinds = std::vector<OverviewItemKind> {};

    for (const auto& item: content.itemsFromTheBottom)
        kinds.push_back(item.kind);

    return kinds;
}

bool isNear(float actual, float expected)
{
    return std::abs(actual - expected) < tolerance;
}
} // namespace

auto tWaitingAreCards = test("Overview/waitingSessionsAreCardsNearestTheThumb") = []
{
    const auto content = arrangeOverview(
        makeInputs({makeSession("cowterm", true), makeSession("miro", false)}));

    check(listKinds(content)
          == std::vector {OverviewItemKind::WaitingCard, OverviewItemKind::SessionRow},
          "one card, then the sessions not waiting as rows");
    check(content.itemsFromTheBottom.front().session.sessionName == "cowterm");
    check(content.itemsFromTheBottom.back().session.sessionName == "miro");
    check(!content.banner);
};

auto tSearchHasNoCards = test("Overview/searchingShowsOnlyMatchingRows") = []
{
    auto inputs = makeInputs({makeSession("cowterm", true), makeSession("miro", false)});
    inputs.query = "miro";
    const auto content = arrangeOverview(inputs);

    check(listKinds(content) == std::vector {OverviewItemKind::SessionRow});
    check(content.itemsFromTheBottom.front().session.sessionName == "miro");
};

auto tUnreachableRow = test("Overview/anUnreachableMachineIsOneRowWithItsReason") = []
{
    auto inputs = makeInputs({makeSession("cowterm", false)});
    inputs.hostCount = 2;
    inputs.unreachableHosts = {{rack, "timed out"}};
    const auto content = arrangeOverview(inputs);

    check(listKinds(content)
          == std::vector {OverviewItemKind::SessionRow, OverviewItemKind::UnreachableHostRow});
    check(content.itemsFromTheBottom.back().unreachable.reason == "timed out");
};

auto tEveryMachineUnreachable = test("Overview/noMachineAnsweringIsOneBannerInstead") = []
{
    auto inputs = makeInputs({});
    inputs.hostCount = 2;
    inputs.unreachableHosts = {{laptop, "timed out"}, {rack, "timed out"}};
    const auto content = arrangeOverview(inputs);

    check(content.banner.has_value());
    check(content.itemsFromTheBottom.empty());
};

auto tNoHostsSetUpIsOneBanner = test("Overview/noHostsSetUpIsOneBannerAndNothingElse") = []
{
    const auto content = arrangeOverview(OverviewInputs {});

    check(content.banner == std::optional<std::string> {"No hosts set up.\nRun just ios-pair on the Mac."});
    check(content.itemsFromTheBottom.empty());
};

auto tFirstItemAtTheBottom = test("Overview/theFirstItemSitsAtTheBottomOfTheList") = []
{
    const auto heights = std::vector {rowHeight, rowHeight};
    const auto bounds = layOutFromTheBottom(heights, list, 0.0f);

    check(isNear(bounds[0].y + bounds[0].h, listTop + listHeight));
    check(bounds[1].y + bounds[1].h < bounds[0].y, "the second stacks above the first");
};

auto tScrollMovesContentDown = test("Overview/scrollingBringsLaterItemsDownIntoView") = []
{
    constexpr auto scrolled = 30.0f;
    const auto heights = std::vector {rowHeight, rowHeight};
    const auto resting = layOutFromTheBottom(heights, list, 0.0f);
    const auto moved = layOutFromTheBottom(heights, list, scrolled);

    check(isNear(moved[1].y, resting[1].y + scrolled));
};

auto tMaximumScroll = test("Overview/onlyContentTallerThanTheListScrolls") = []
{
    constexpr auto fewRows = 3;
    constexpr auto manyRows = 20;
    const auto few = std::vector<float>(fewRows, rowHeight);
    const auto many = std::vector<float>(manyRows, rowHeight);

    check(calculateMaximumOverviewScroll(few, listHeight) == 0.0f);
    check(calculateMaximumOverviewScroll(many, listHeight) > 0.0f);
};

auto tTapFindsItem = test("Overview/aTapFindsTheItemUnderIt") = []
{
    const auto heights = std::vector {rowHeight, rowHeight};
    const auto bounds = layOutFromTheBottom(heights, list, 0.0f);

    check(findOverviewItemAt(bounds, bounds[1].center()) == std::size_t {1});
    check(!findOverviewItemAt(bounds, {listWidth / 2.0f, listTop}).has_value(),
          "the empty top of a short list is nothing");
};
