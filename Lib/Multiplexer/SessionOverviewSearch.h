#pragma once

#include "SessionOverview.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace term::overview
{
// A row's decayed usage from openings recorded on the phone, by row key.
using SessionOpeningUsage = std::function<double(const std::string& rowKey)>;

// "<host>/<session>": one row per session per machine.
std::string makeOverviewRowKey(const OverviewRow& row);

// The rows `query` matches, in the Mac palette's order and by its match rules
// (rankPaletteItems); an empty query keeps every row, ordered by usage. A row's usage is how
// often and how recently it was opened on the phone, plus its last typing as
// one use; rows with equal usage keep the overview's order.
std::vector<OverviewRow> searchOverviewRows(const std::vector<OverviewRow>& rows,
                                            const std::string& query,
                                            std::int64_t nowMilliseconds,
                                            const SessionOpeningUsage& openingUsage);
} // namespace term::overview
