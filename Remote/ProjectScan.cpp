#include "ProjectScan.h"

#include <filesystem>
#include <unordered_set>

namespace term
{
namespace fs = std::filesystem;

std::vector<std::string> listProjectDirectories(const std::vector<std::string>& roots)
{
    auto result = std::vector<std::string> {};
    auto seen = std::unordered_set<std::string> {};

    auto add = [&](const fs::path& dir)
    {
        auto path = dir.generic_string();

        if (seen.insert(path).second)
            result.push_back(path);
    };

    for (const auto& configured: roots)
    {
        const auto root = fs::path(configured);
        auto error = std::error_code {};

        if (!fs::is_directory(root, error))
            continue;

        add(root);

        for (auto& entry: fs::directory_iterator(root, error))
        {
            if (!entry.is_directory(error))
                continue;

            add(entry.path());
        }
    }

    return result;
}
} // namespace term
