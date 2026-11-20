#pragma once

#include <ctime>

namespace term::push
{
// The date and clock time in this machine's time zone; thread-safe, unlike std::localtime.
std::tm getLocalTime(std::time_t time);
} // namespace term::push
