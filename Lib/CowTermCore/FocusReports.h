#pragma once

#include <string_view>

namespace term
{
// Mode 1004: tells the program when its pane gains (CSI I) or loses (CSI O)
// the user's focus. Remembers the last state so only changes are reported.
class FocusReports
{
public:
    // The bytes to send for the pane's focus now; empty when nothing changed
    // or the program hasn't asked for reports.
    std::string_view update(bool focused, bool reportsWanted);

private:
    bool lastFocused = false;
};
} // namespace term
