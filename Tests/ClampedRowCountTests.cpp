#include "ClampedRowCount.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

namespace term
{
[[gnu::noinline]] void ClampedRowCount::receiveRows(int rows)
{
    receivedRows = rows;
}
} // namespace term

auto tClampedRowCountArrives = test("Build/aClampedRowCountArrivesUnchanged") = []
{
    constexpr auto startRows = 24;
    constexpr auto paneRows = 8;
    auto clamped = ClampedRowCount {startRows};

    check(clamped.receivedRows == startRows);

    clamped.resize(paneRows);

    check(clamped.receivedRows == paneRows);
};
