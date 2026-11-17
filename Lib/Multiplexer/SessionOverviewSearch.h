#pragma once

#include "SessionOverview.h"

#include <cstdint>
#include <string>
#include <vector>

namespace term::overview
{
// The rows `query` matches, in the Mac palette's order and by its match rules
// (rankPaletteItems). A row's usage is its last typing, decayed to now. An
// empty query keeps the overview's own order.
std::vector<OverviewRow> searchOverviewRows(const std::vector<OverviewRow>& rows,
                                            const std::string& query,
                                            std::int64_t nowMilliseconds);
} // namespace term::overview
