#include "ProcessEnvironment.h"

#include <fstream>

namespace term
{
std::optional<std::string> readProcessEnvironmentVariable(std::int64_t processId,
                                                          const std::string& name)
{
    auto file = std::ifstream {"/proc/" + std::to_string(processId) + "/environ"};
    const auto prefix = name + "=";
    auto entry = std::string {};

    while (std::getline(file, entry, '\0'))
    {
        if (entry.starts_with(prefix))
            return entry.substr(prefix.size());
    }

    return std::nullopt;
}
} // namespace term
