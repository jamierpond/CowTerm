#include "GitBranch.h"

#include <filesystem>
#include <fstream>
#include <optional>

namespace term
{
namespace
{
namespace fs = std::filesystem;

std::string firstLine(const fs::path& file)
{
    auto in = std::ifstream {file};
    auto line = std::string {};
    std::getline(in, line);

    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();

    return line;
}

struct GitCheckout
{
    fs::path root;
    fs::path head;
};

// The nearest ".git" up the tree from `dir`, either the git dir itself or
// (worktrees, submodules) a file pointing at it.
std::optional<GitCheckout> findGitCheckout(const std::string& dir)
{
    if (dir.empty())
        return std::nullopt;

    auto error = std::error_code {};

    for (auto at = fs::path {dir}; !at.empty(); at = at.parent_path())
    {
        const auto dotGit = at / ".git";

        if (fs::is_directory(dotGit, error))
            return GitCheckout {at, dotGit / "HEAD"};

        if (fs::is_regular_file(dotGit, error))
        {
            constexpr auto prefix = std::string_view {"gitdir: "};
            const auto line = firstLine(dotGit);

            if (!line.starts_with(prefix))
                return std::nullopt;

            auto gitDir = fs::path {line.substr(prefix.size())};
            return GitCheckout {at, (gitDir.is_relative() ? at / gitDir : gitDir) / "HEAD"};
        }

        if (at == at.parent_path())
            break;
    }

    return std::nullopt;
}

std::optional<fs::path> headFile(const std::string& dir)
{
    if (auto checkout = findGitCheckout(dir))
        return checkout->head;

    return std::nullopt;
}

// "ref: refs/heads/<branch>"; anything else is a detached HEAD.
std::optional<std::string> branchIn(const fs::path& head)
{
    auto error = std::error_code {};

    if (!fs::is_regular_file(head, error))
        return std::nullopt;

    constexpr auto prefix = std::string_view {"ref: refs/heads/"};
    const auto line = firstLine(head);
    return line.starts_with(prefix) ? line.substr(prefix.size()) : std::string {};
}
} // namespace

std::string gitBranch(const std::string& dir)
{
    const auto head = headFile(dir);
    return head ? branchIn(*head).value_or("") : std::string {};
}

std::string gitRepositoryRoot(const std::string& dir)
{
    const auto checkout = findGitCheckout(dir);
    return checkout ? checkout->root.string() : std::string {};
}

std::string GitBranches::of(const std::string& dir)
{
    if (const auto known = heads.find(dir); known != heads.end())
    {
        if (auto branch = branchIn(known->second))
            return *branch;

        heads.erase(known); // the repo or worktree went away
    }

    // Only repos are remembered: a directory that becomes one (git init,
    // clone) is found on a later lookup.
    const auto head = headFile(dir);

    if (!head)
        return {};

    heads[dir] = head->string();
    return branchIn(*head).value_or("");
}

std::string GitBranches::repositoryRootOf(const std::string& dir)
{
    if (const auto known = roots.find(dir); known != roots.end())
    {
        auto error = std::error_code {};

        if (fs::exists(fs::path {known->second} / ".git", error))
            return known->second;

        roots.erase(known);
    }

    auto root = gitRepositoryRoot(dir);

    if (!root.empty())
        roots[dir] = root;

    return root;
}
} // namespace term
