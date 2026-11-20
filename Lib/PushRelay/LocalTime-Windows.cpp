#include "LocalTime.h"

namespace term::push
{
std::tm getLocalTime(std::time_t time)
{
    auto localTime = std::tm {};
    localtime_s(&localTime, &time);
    return localTime;
}
} // namespace term::push
