#pragma once

#include "Multiplexer/SessionOverview.h"

#include <eacp/Graphics/Primitives/Primitives.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// What the overview shows and where, as plain maths the Mac's tests can check.
// The list is anchored at the bottom: item 0 sits nearest the thumb, by the
// search field, and later items stack upwards.
namespace cowterm::mobile
{
struct UnreachableHost
{
    std::string host;
    std::string reason;
};

enum class OverviewItemKind
{
    WaitingCard,
    SessionRow,
    UnreachableHostRow
};

struct OverviewItem
{
    OverviewItemKind kind = OverviewItemKind::SessionRow;
    term::overview::OverviewRow session;
    UnreachableHost unreachable;
};

struct OverviewContent
{
    std::vector<OverviewItem> itemsFromTheBottom;

    // Shown alone, instead of the list, when no host is set up or none
    // answers; one line per '\n'.
    std::optional<std::string> banner;
};

struct OverviewInputs
{
    std::vector<term::overview::OverviewRow> sessions;
    std::vector<UnreachableHost> unreachableHosts;
    int hostCount = 0;
    std::string query;
    std::int64_t nowMilliseconds = 0;
};

// With no query: the waiting sessions as cards nearest the thumb, then the
// other sessions as rows in the overview's order (a waiting session is not
// shown twice), then one row per unreachable machine. With a query: only the matching sessions, in the Mac palette's
// order. When no host is set up, or every machine is unreachable: a banner
// saying so and nothing else.
OverviewContent arrangeOverview(const OverviewInputs& inputs);

float getOverviewItemHeight(OverviewItemKind kind);

std::vector<float> listOverviewItemHeights(const std::vector<OverviewItem>& items);

// How far the list can scroll: its content's height past the list's own.
float calculateMaximumOverviewScroll(const std::vector<float>& heights, float listHeight);

// Each item's place, the first at the bottom of the list. scrollOffset moves
// the content down, bringing later items into view from the top.
std::vector<eacp::Graphics::Rect> layOutFromTheBottom(const std::vector<float>& heights,
                                                      eacp::Graphics::Rect list,
                                                      float scrollOffset);

std::optional<std::size_t> findOverviewItemAt(const std::vector<eacp::Graphics::Rect>& itemBounds,
                                              eacp::Graphics::Point position);
} // namespace cowterm::mobile
