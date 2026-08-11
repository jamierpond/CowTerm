#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace term
{
// Opt-in tracing for the popup sizing investigation. Set COWTERM_POPUP_DEBUG=1
// (logs to /tmp/cowterm-popup.log) or to a path of your choosing. A no-op when
// the variable is unset, so it is inert on the normal launch path.
inline void debugLog(const char* fmt, ...)
{
    const auto* dest = std::getenv("COWTERM_POPUP_DEBUG");

    if (dest == nullptr)
        return;

    const auto* path =
        (dest[0] == '\0' || (dest[0] == '1' && dest[1] == '\0'))
            ? "/tmp/cowterm-popup.log"
            : dest;

    auto* file = std::fopen(path, "a");

    if (file == nullptr)
        return;

    va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);

    std::fclose(file);
}
} // namespace term
