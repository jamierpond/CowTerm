#include "FocusReports.h"

namespace term
{
std::string_view FocusReports::update(bool focused, bool reportsWanted)
{
    if (focused == lastFocused)
        return {};

    lastFocused = focused;

    if (!reportsWanted)
        return {};

    if (focused)
        return "\033[I";

    return "\033[O";
}
} // namespace term
