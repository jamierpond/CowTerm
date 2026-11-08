#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace term::fuzz
{
inline std::string readWholeFile(const std::filesystem::path& path)
{
    auto stream = std::ifstream {path, std::ios::binary};
    return {std::istreambuf_iterator<char> {stream}, std::istreambuf_iterator<char> {}};
}

inline std::vector<std::filesystem::path> listCorpusFiles(const std::filesystem::path& directory)
{
    auto files = std::vector<std::filesystem::path> {};

    for (const auto& entry: std::filesystem::directory_iterator {directory})
    {
        if (entry.is_regular_file())
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());
    return files;
}

// Snapshots.txt holds one "fileName hash" pair per line.
inline std::map<std::string, std::uint64_t> readSnapshots(const std::filesystem::path& path)
{
    auto snapshots = std::map<std::string, std::uint64_t> {};
    auto stream = std::ifstream {path};
    auto name = std::string {};
    auto hash = std::uint64_t {};

    while (stream >> name >> hash)
        snapshots[name] = hash;

    return snapshots;
}
} // namespace term::fuzz
