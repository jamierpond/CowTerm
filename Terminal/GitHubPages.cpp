#include "GitHubPages.h"

#include <eacp/Core/Utils/Strings.h>
#include <eacp/Network/HTTP/Http.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <string_view>
#include <utility>

namespace term::github
{
namespace
{
constexpr auto gitHubPageRoot = std::string_view {"https://github.com/"};
constexpr auto remoteDefaultBranchPrefix = std::string_view {"origin/"};

constexpr auto gitHubRemotePrefixes = std::array {
    std::string_view {"git@github.com:"},
    std::string_view {"ssh://git@github.com/"},
    std::string_view {"https://github.com/"},
    std::string_view {"http://github.com/"},
    std::string_view {"git://github.com/"},
};

// Where gh usually lives when a non-login ssh shell's PATH leaves it out.
constexpr auto extraToolDirectories = std::string_view {
    "/opt/homebrew/bin:/usr/local/bin:$HOME/.local/bin:$HOME/bin:$HOME/scoop/shims"};

constexpr auto sshConnectTimeoutOption = std::string_view {"ConnectTimeout=10"};

using FactField = std::pair<std::string_view, std::string RepositoryFacts::*>;

constexpr auto factFields = std::array {
    FactField {"remote", &RepositoryFacts::remoteUrl},
    FactField {"branch", &RepositoryFacts::branch},
    FactField {"default", &RepositoryFacts::defaultBranch},
    FactField {"pr", &RepositoryFacts::pullRequestUrl},
};

using FolderProblemName = std::pair<std::string_view, FolderProblem>;

constexpr auto folderProblemNames = std::array {
    FolderProblemName {"folder-missing", FolderProblem::folderMissing},
    FolderProblemName {"not-a-repository", FolderProblem::notARepository},
};

bool isOwnerSlashRepository(std::string_view path)
{
    const auto hasOneInnerSlash = std::ranges::count(path, '/') == 1;
    return hasOneInnerSlash && !path.starts_with('/') && !path.ends_with('/');
}

// eacp's urlEncode escapes '/' too, but a branch's slashes read as slashes on GitHub.
std::string escapeBranchForUrl(const std::string& branch)
{
    constexpr auto escapedSlash = std::string_view {"%2F"};

    auto escaped = eacp::HTTP::urlEncode(branch);

    for (auto at = escaped.find(escapedSlash); at != std::string::npos;
         at = escaped.find(escapedSlash, at))
        escaped.replace(at, escapedSlash.size(), "/");

    return escaped;
}

// Single quotes keep the folder literal; a leading ~ stays outside them so
// the shell still expands it.
std::string quoteDirectoryForShell(const std::string& directory)
{
    auto quoted = std::string {};
    auto rest = std::string_view {directory};

    if (rest == "~" || rest.starts_with("~/"))
    {
        quoted = "\"$HOME\"";
        rest.remove_prefix(1);
    }

    quoted += "'";

    for (const auto character: rest)
    {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }

    return quoted + "'";
}

FolderProblem findFolderProblem(std::string_view name)
{
    const auto found =
        std::ranges::find(folderProblemNames, name, &FolderProblemName::first);

    if (found == folderProblemNames.end())
        return FolderProblem::none;

    return found->second;
}

void storeFact(RepositoryFacts& facts, const std::string& line)
{
    const auto equals = line.find('=');

    if (equals == std::string::npos)
        return;

    const auto key = std::string_view {line}.substr(0, equals);
    const auto value =
        eacp::Strings::trim(std::string_view {line}.substr(equals + 1));

    if (key == "problem")
    {
        facts.problem = findFolderProblem(value);
        return;
    }

    if (const auto field = std::ranges::find(factFields, key, &FactField::first);
        field != factFields.end())
        facts.*(field->second) = value;
}

std::string describeFolderProblem(FolderProblem problem)
{
    if (problem == FolderProblem::folderMissing)
        return "The pane's folder no longer exists.";

    return "The pane's folder is not in a git repository.";
}

PageChoice choosePullRequestPage(const RepositoryFacts& facts,
                                 const std::string& repositoryPage)
{
    if (!facts.pullRequestUrl.empty())
        return {.url = facts.pullRequestUrl};

    if (facts.branch.empty())
        return {
            .failureReason =
                "The pane is on a detached HEAD, so there is no branch to open a pull request for."};

    if (facts.branch == facts.defaultBranch)
        return {.failureReason =
                    "The pane is on " + facts.branch
                    + ", the default branch, so there is no pull request to open."};

    return {.url =
                makeCompareUrl(repositoryPage, facts.defaultBranch, facts.branch)};
}
} // namespace

std::string convertRemoteUrlToRepositoryPage(const std::string& remoteUrl)
{
    const auto trimmedText = eacp::Strings::trim(remoteUrl);
    auto trimmed = std::string_view {trimmedText};

    if (trimmed.ends_with('/'))
        trimmed.remove_suffix(1);

    if (trimmed.ends_with(".git"))
        trimmed.remove_suffix(std::string_view {".git"}.size());

    for (const auto prefix: gitHubRemotePrefixes)
    {
        if (!trimmed.starts_with(prefix))
            continue;

        const auto path = trimmed.substr(prefix.size());

        if (isOwnerSlashRepository(path))
            return std::string {gitHubPageRoot} + std::string {path};
    }

    return {};
}

std::string makeCompareUrl(const std::string& repositoryPage,
                           const std::string& defaultBranch,
                           const std::string& branch)
{
    auto range = escapeBranchForUrl(branch);

    if (!defaultBranch.empty())
        range = escapeBranchForUrl(defaultBranch) + "..." + range;

    return repositoryPage + "/compare/" + range + "?expand=1";
}

std::string makeRepositoryQueryScript(const PaneFolder& folder, PageKind kind)
{
    auto script =
        "PATH=\"$PATH:" + std::string {extraToolDirectories}
        + "\"\n"
          "cd -- "
        + quoteDirectoryForShell(folder.directory)
        + " 2>/dev/null || { echo problem=folder-missing; exit 0; }\n"
          "git rev-parse --git-dir </dev/null >/dev/null 2>&1"
          " || { echo problem=not-a-repository; exit 0; }\n"
          "echo \"remote=$(git remote get-url origin </dev/null 2>/dev/null)\"\n"
          "echo \"branch=$(git symbolic-ref --quiet --short HEAD </dev/null 2>/dev/null)\"\n"
          "echo \"default=$(git symbolic-ref --quiet --short refs/remotes/origin/HEAD"
          " </dev/null 2>/dev/null)\"\n";

    if (kind == PageKind::pullRequest)
        script +=
            "if command -v gh >/dev/null 2>&1; then\n"
            "  echo \"pr=$(GH_PROMPT_DISABLED=1 gh pr view --json url --jq .url"
            " </dev/null 2>/dev/null)\"\n"
            "fi\n";

    return script;
}

std::vector<std::string> makeRepositoryQueryCommand(const PaneFolder& folder,
                                                    PageKind kind)
{
    auto script = makeRepositoryQueryScript(folder, kind);

    if (folder.host.empty())
        return {"/bin/sh", "-c", std::move(script)};

    return {"ssh",
            "-n",
            "-o",
            "BatchMode=yes",
            "-o",
            std::string {sshConnectTimeoutOption},
            folder.host,
            std::move(script)};
}

RepositoryFacts parseRepositoryQueryOutput(const std::string& output)
{
    auto facts = RepositoryFacts {};
    auto lines = std::istringstream {output};
    auto line = std::string {};

    while (std::getline(lines, line))
        storeFact(facts, line);

    if (facts.defaultBranch.starts_with(remoteDefaultBranchPrefix))
        facts.defaultBranch.erase(0, remoteDefaultBranchPrefix.size());

    return facts;
}

bool isQueryUnanswered(const RepositoryFacts& facts)
{
    return facts.problem == FolderProblem::none && facts.remoteUrl.empty()
           && facts.branch.empty();
}

PageChoice choosePage(const RepositoryFacts& facts, PageKind kind)
{
    if (isQueryUnanswered(facts))
        return {.failureReason = "Could not read the pane's repository."};

    if (facts.problem != FolderProblem::none)
        return {.failureReason = describeFolderProblem(facts.problem)};

    if (facts.remoteUrl.empty())
        return {.failureReason = "The repository has no origin remote."};

    const auto repositoryPage = convertRemoteUrlToRepositoryPage(facts.remoteUrl);

    if (repositoryPage.empty())
        return {.failureReason =
                    "origin is " + facts.remoteUrl + ", which is not on GitHub."};

    if (kind == PageKind::repository)
        return {.url = repositoryPage};

    return choosePullRequestPage(facts, repositoryPage);
}
} // namespace term::github
