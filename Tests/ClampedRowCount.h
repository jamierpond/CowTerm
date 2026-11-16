#pragma once

namespace term
{
// TermScreen::resize's shape, cut down to the argument GCC 15.3 miscompiles:
// a constructor and a resize that each clamp the row count with std::max and
// pass it on to a function in another file.
struct ClampedRowCount
{
    explicit ClampedRowCount(int rows);

    void resize(int requestedRows);
    void receiveRows(int rows);

    int receivedRows = 0;
};
} // namespace term
