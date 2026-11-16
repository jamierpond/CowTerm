#include "ClampedRowCount.h"

#include <algorithm>

namespace term
{
[[gnu::noinline]] ClampedRowCount::ClampedRowCount(int rows)
{
    resize(std::max(rows, 1));
}

[[gnu::noinline]] void ClampedRowCount::resize(int requestedRows)
{
    receiveRows(std::max(requestedRows, 1));
}
} // namespace term
