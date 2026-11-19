#include "LocalHostName.h"

namespace term
{
std::string getThisMachineName(const AppConfig& config)
{
    if (config.hostName.empty())
        return getShortLocalHostName();

    return config.hostName;
}
} // namespace term
