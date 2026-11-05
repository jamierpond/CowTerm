#pragma once

#include <Miro/Reflect.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// A repository a brief can target: a git checkout in ~/projects, a GitHub
// repository of Jamie's or one of his organisations, or both.
struct Project
{
    std::string name;
    std::string nameWithOwner;
    std::string localPath;

    MIRO_REFLECT(name, nameWithOwner, localPath)
};

struct ProjectList
{
    std::vector<Project> projects;

    MIRO_REFLECT(projects)
};

bool isProjectLocal(const Project& project);
std::string describeProject(const Project& project);

// Every direct child of `projectsDirectory` with a .git directory (worktrees,
// whose .git is a file, are left out).
std::vector<Project> listLocalProjects(const std::string& projectsDirectory);

// `gh api user/orgs` and `gh repo list --json nameWithOwner,name`.
std::vector<std::string> parseOrganisationLogins(std::string_view json);
std::vector<Project> parseRepositoryList(std::string_view json);

// Local checkouts first; a remote repository with a local checkout of the same
// name becomes that checkout, carrying the remote name.
std::vector<Project> mergeProjects(std::vector<Project> local, const std::vector<Project>& remote);

struct GitHubTools
{
    std::string ghExecutable;
};

// Blocking: runs gh for Jamie, then once per organisation. Call off the main thread.
std::vector<Project> listRemoteProjects(const GitHubTools& tools);

// Best fuzzyScore first (the palette's scorer).
std::vector<Project> filterProjects(const std::vector<Project>& projects, std::string_view query);

// "Fix the auth redirect" -> "jp/fix-the-auth-redirect".
std::string makeTaskBranchName(std::string_view title);

// ~/projects/<repo>-<branch with / as ->.
std::string makeWorktreePath(const std::string& projectsDirectory,
                             const Project& project,
                             const std::string& taskBranch);

std::string makeClonePath(const std::string& projectsDirectory, const Project& project);

// One step of getting a worktree ready: a program and its arguments.
struct PreparationStep
{
    std::string description;
    std::string executable;
    std::vector<std::string> arguments;
};

struct WorkspaceRequest
{
    Project project;
    std::string projectsDirectory;
    std::string taskBranch;
    std::string gitExecutable;
    std::string ghExecutable;
};

// Clone when not local, then fetch. The default branch is read after these.
std::vector<PreparationStep> makeCloneAndFetchSteps(const WorkspaceRequest& request);

// `refs/remotes/origin/HEAD` -> "main"; empty when unreadable.
std::string parseDefaultBranchReference(std::string_view symbolicReference);

PreparationStep makeWorktreeStep(const WorkspaceRequest& request, const std::string& defaultBranch);

struct PreparedWorkspace
{
    bool hasSucceeded = false;
    std::string problem;
    std::string worktreePath;
    std::string defaultBranch;
};

// Blocking: clones, fetches, reads the default branch and adds the worktree,
// reporting each step's description first. Call off the main thread.
PreparedWorkspace prepareWorkspace(const WorkspaceRequest& request,
                                   const std::function<void(const std::string&)>& reportProgress);
} // namespace term
