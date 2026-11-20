#pragma once

#include <string>
#include <vector>

// Ctrl+A g and Ctrl+A r: which GitHub page belongs to a pane's folder, found by
// one small shell command run where the pane lives (locally or over ssh).
namespace term::github
{
enum class PageKind
{
    repository,
    pullRequest
};

enum class FolderProblem
{
    none,
    folderMissing,
    notARepository
};

// What the repository query printed about the pane's folder. Empty strings
// for what it could not find: no origin, a detached HEAD, no origin/HEAD,
// gh missing or finding no pull request.
struct RepositoryFacts
{
    FolderProblem problem = FolderProblem::none;
    std::string remoteUrl;
    std::string branch;
    std::string defaultBranch;
    std::string pullRequestUrl;
};

// Where a pane's shell is: host is empty for this machine, otherwise the ssh
// alias the session reaches it by.
struct PaneFolder
{
    std::string host;
    std::string directory;
};

// The page to open, or why there is none.
struct PageChoice
{
    std::string url;
    std::string failureReason;
};

// "https://github.com/owner/repo" for any of git@github.com:owner/repo.git,
// ssh://git@github.com/owner/repo.git and https://github.com/owner/repo(.git);
// empty for a remote that is not on GitHub.
std::string convertRemoteUrlToRepositoryPage(const std::string& remoteUrl);

// GitHub's "open a pull request" form for branch; without a default branch,
// GitHub picks the repository's own.
std::string makeCompareUrl(const std::string& repositoryPage,
                           const std::string& defaultBranch,
                           const std::string& branch);

// A POSIX shell script that prints the facts as key=value lines; it asks gh
// for the branch's pull request only for PageKind::pullRequest.
std::string makeRepositoryQueryScript(const PaneFolder& folder, PageKind kind);

// The program and arguments that run the query: sh here when host is empty,
// otherwise ssh to host, whose own shell runs the script.
std::vector<std::string> makeRepositoryQueryCommand(const PaneFolder& folder,
                                                    PageKind kind);

RepositoryFacts parseRepositoryQueryOutput(const std::string& output);

// True when the output held none of the query's lines: ssh or the shell
// failed before the script ran.
bool isQueryUnanswered(const RepositoryFacts& facts);

PageChoice choosePage(const RepositoryFacts& facts, PageKind kind);
} // namespace term::github
