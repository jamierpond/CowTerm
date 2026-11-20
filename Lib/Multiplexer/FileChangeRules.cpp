#include "FileChangeRules.h"

#include <algorithm>
#include <array>

namespace term::filechanges
{
namespace
{
namespace fs = std::filesystem;

constexpr auto gitFolderName = ".git";
constexpr auto foldersWatchedOneSubfolderAtATime = std::size_t {10};
constexpr auto generatedFolderNames = std::array {"node_modules", "build*", "_deps", "target", ".venv"};

fs::path removeTrailingSeparator(const fs::path& folder)
{
    const auto normal = folder.lexically_normal();

    if (!normal.has_filename() && normal.has_relative_path())
        return normal.parent_path();

    return normal;
}

bool isSameFolder(const fs::path& first, const fs::path& second)
{
    auto error = std::error_code {};
    return fs::equivalent(first, second, error);
}

bool hasGitEntry(const fs::path& folder)
{
    auto error = std::error_code {};
    return fs::exists(folder / gitFolderName, error);
}
} // namespace

bool isTooBroadToWatch(const fs::path& folder, const fs::path& homeFolder)
{
    auto error = std::error_code {};

    if (folder.empty() || !fs::is_directory(folder, error))
        return true;

    return isSameFolder(folder, homeFolder) || isSameFolder(folder, folder.root_path());
}

std::string findProjectFolder(const fs::path& workingDirectory, const fs::path& homeFolder)
{
    const auto start = removeTrailingSeparator(workingDirectory);

    for (auto folder = start; !isTooBroadToWatch(folder, homeFolder); folder = folder.parent_path())
    {
        if (hasGitEntry(folder))
            return folder.string();
    }

    return {};
}

std::vector<std::string> orderFoldersByLastInput(std::vector<FolderLastInput> folders)
{
    std::ranges::stable_sort(folders, std::ranges::greater {}, &FolderLastInput::lastInputMilliseconds);
    auto ordered = std::vector<std::string> {};

    for (const auto& folder: folders)
    {
        if (!folder.folder.empty() && std::ranges::find(ordered, folder.folder) == ordered.end())
            ordered.push_back(folder.folder);
    }

    return ordered;
}

std::vector<std::string>
    chooseFoldersToWatchOneSubfolderAtATime(const std::vector<std::string>& foldersByLastInput)
{
    const auto count = std::min(foldersByLastInput.size(), foldersWatchedOneSubfolderAtATime);
    const auto countAsOffset = static_cast<std::ptrdiff_t>(count);
    return {foldersByLastInput.begin(), foldersByLastInput.begin() + countAsOffset};
}

std::vector<std::string> listExcludedFolderNames(bool isWatchingEachSubfolder)
{
    auto names = std::vector<std::string> {gitFolderName};

    if (isWatchingEachSubfolder)
        names.insert(names.end(), generatedFolderNames.begin(), generatedFolderNames.end());

    return names;
}

std::string findWatchedFolderOfChange(const fs::path& changed, const std::vector<std::string>& watchedFolders)
{
    auto innermost = std::string {};

    for (const auto& folder: watchedFolders)
    {
        const auto inside = changed.lexically_relative(removeTrailingSeparator(folder));
        const auto isInside = !inside.empty() && *inside.begin() != "..";

        if (isInside && folder.size() > innermost.size())
            innermost = folder;
    }

    return innermost;
}
} // namespace term::filechanges
