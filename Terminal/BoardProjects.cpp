#include "BoardProjects.h"

#include "FuzzyMatch.h"

#include <JsonFields.h>

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace term
{
namespace
{
constexpr auto taskBranchPrefix = std::string_view {"jp/"};
constexpr auto remoteHeadPrefix = std::string_view {"refs/remotes/origin/"};
constexpr auto repositoryListLimit = std::string_view {"500"};
constexpr auto maximumBranchStemLength = std::size_t {60};
constexpr auto untitledBranchStem = std::string_view {"task"};

// "Fix the auth redirect!" -> "fix-the-auth-redirect".
std::string makeBranchStem(std::string_view title)
{
    auto stem = std::string {};
    auto isPendingDash = false;

    for (auto character: title)
    {
        auto byte = static_cast<unsigned char>(character);

        if (std::isalnum(byte) != 0)
        {
            if (isPendingDash && !stem.empty())
                stem += '-';

            isPendingDash = false;
            stem += static_cast<char>(std::tolower(byte));
        }
        else
        {
            isPendingDash = true;
        }

        if (stem.size() >= maximumBranchStemLength)
            break;
    }

    if (stem.empty())
        return std::string {untitledBranchStem};

    return stem;
}

std::string getWorkingCopyPath(const WorkspaceRequest& request)
{
    if (isProjectLocal(request.project))
        return request.project.localPath;

    return makeClonePath(request.projectsDirectory, request.project);
}

eacp::Processes::ProcessResult runStep(const PreparationStep& step)
{
    auto options = eacp::Processes::ProcessOptions {};
    options.executable = step.executable;
    options.arguments = step.arguments;
    options.mergeErrorOutput = true;
    return eacp::Processes::run(options);
}

std::vector<Project> runRepositoryList(const GitHubTools& tools, const std::string& owner)
{
    auto result = eacp::Processes::run(
        tools.ghExecutable,
        {"repo", "list", owner, "--limit", std::string {repositoryListLimit}, "--json", "nameWithOwner,name"});

    if (!result.launched || result.exitCode != 0)
        return {};

    return parseRepositoryList(result.output);
}
} // namespace

bool isProjectLocal(const Project& project)
{
    return !project.localPath.empty();
}

std::string describeProject(const Project& project)
{
    if (project.nameWithOwner.empty())
        return project.name;

    if (isProjectLocal(project))
        return project.name + "  (" + project.nameWithOwner + ")";

    return project.nameWithOwner + "  (clone)";
}

std::vector<Project> listLocalProjects(const std::string& projectsDirectory)
{
    auto projects = std::vector<Project> {};

    for (const auto& entry: eacp::Files::listDirectory(eacp::FilePath {projectsDirectory}))
    {
        auto path = entry.path;

        if (!eacp::Files::isDirectory(path) || !eacp::Files::isDirectory(path / ".git"))
            continue;

        auto project = Project {};
        project.name = path.fileName();
        project.localPath = path.str();
        projects.push_back(std::move(project));
    }

    std::ranges::sort(projects, {}, &Project::name);
    return projects;
}

std::vector<std::string> parseOrganisationLogins(std::string_view json)
{
    auto logins = std::vector<std::string> {};
    auto value = Miro::Json::getParsedValue(json);

    if (!value.isArray())
        return logins;

    for (const auto& organisation: value.asArray())
    {
        auto login = jsonfields::readStringField(organisation, "login");

        if (!login.empty())
            logins.push_back(std::move(login));
    }

    return logins;
}

std::vector<Project> parseRepositoryList(std::string_view json)
{
    auto projects = std::vector<Project> {};
    auto value = Miro::Json::getParsedValue(json);

    if (!value.isArray())
        return projects;

    for (const auto& repository: value.asArray())
    {
        auto project = Project {};
        project.name = jsonfields::readStringField(repository, "name");
        project.nameWithOwner = jsonfields::readStringField(repository, "nameWithOwner");

        if (!project.name.empty() && !project.nameWithOwner.empty())
            projects.push_back(std::move(project));
    }

    return projects;
}

std::vector<Project> mergeProjects(std::vector<Project> local, const std::vector<Project>& remote)
{
    auto localNames = std::set<std::string> {};

    for (const auto& project: local)
        localNames.insert(project.name);

    for (const auto& project: remote)
    {
        if (!localNames.contains(project.name))
        {
            local.push_back(project);
            continue;
        }

        auto found = std::ranges::find(local, project.name, &Project::name);

        if (found->nameWithOwner.empty())
            found->nameWithOwner = project.nameWithOwner;
    }

    return local;
}

std::vector<Project> listRemoteProjects(const GitHubTools& tools)
{
    auto projects = std::vector<Project> {};
    auto user = eacp::Processes::run(tools.ghExecutable, {"api", "user", "--jq", ".login"});

    if (!user.launched || user.exitCode != 0)
        return projects;

    auto owners = std::vector<std::string> {eacp::Strings::trim(user.output)};
    auto organisations = eacp::Processes::run(tools.ghExecutable, {"api", "user/orgs"});

    if (organisations.launched && organisations.exitCode == 0)
    {
        for (auto& login: parseOrganisationLogins(organisations.output))
            owners.push_back(std::move(login));
    }

    for (const auto& owner: owners)
    {
        for (auto& project: runRepositoryList(tools, owner))
            projects.push_back(std::move(project));
    }

    return projects;
}

std::vector<Project> filterProjects(const std::vector<Project>& projects, std::string_view query)
{
    struct ScoredProject
    {
        int score;
        std::size_t order;
    };

    auto scored = std::vector<ScoredProject> {};

    for (std::size_t index = 0; index < projects.size(); ++index)
    {
        if (auto score = fuzzyScore(std::string {query}, describeProject(projects[index])))
            scored.push_back({*score, index});
    }

    std::ranges::stable_sort(scored, std::ranges::greater {}, &ScoredProject::score);

    auto filtered = std::vector<Project> {};

    for (const auto& entry: scored)
        filtered.push_back(projects[entry.order]);

    return filtered;
}

std::string makeTaskBranchName(std::string_view title)
{
    return std::string {taskBranchPrefix} + makeBranchStem(title);
}

std::string makeWorktreePath(const std::string& projectsDirectory,
                             const Project& project,
                             const std::string& taskBranch)
{
    auto branchPart = taskBranch;
    std::ranges::replace(branchPart, '/', '-');
    return projectsDirectory + "/" + project.name + "-" + branchPart;
}

std::string makeClonePath(const std::string& projectsDirectory, const Project& project)
{
    return projectsDirectory + "/" + project.name;
}

std::vector<PreparationStep> makeCloneAndFetchSteps(const WorkspaceRequest& request)
{
    auto steps = std::vector<PreparationStep> {};
    auto path = getWorkingCopyPath(request);

    if (!isProjectLocal(request.project))
    {
        steps.push_back({"cloning " + request.project.nameWithOwner,
                         request.ghExecutable,
                         {"repo", "clone", request.project.nameWithOwner, path}});
    }

    steps.push_back({"fetching origin", request.gitExecutable, {"-C", path, "fetch", "origin"}});
    steps.push_back({"reading the default branch",
                     request.gitExecutable,
                     {"-C", path, "remote", "set-head", "origin", "--auto"}});
    return steps;
}

std::string parseDefaultBranchReference(std::string_view symbolicReference)
{
    auto reference = eacp::Strings::trim(symbolicReference);

    if (!reference.starts_with(remoteHeadPrefix))
        return {};

    return reference.substr(remoteHeadPrefix.size());
}

PreparationStep makeWorktreeStep(const WorkspaceRequest& request, const std::string& defaultBranch)
{
    auto worktreePath =
        makeWorktreePath(request.projectsDirectory, request.project, request.taskBranch);
    return {"making worktree " + worktreePath,
            request.gitExecutable,
            {"-C",
             getWorkingCopyPath(request),
             "worktree",
             "add",
             "-b",
             request.taskBranch,
             worktreePath,
             "origin/" + defaultBranch}};
}

PreparedWorkspace prepareWorkspace(const WorkspaceRequest& request,
                                   const std::function<void(const std::string&)>& reportProgress)
{
    auto prepared = PreparedWorkspace {};
    auto runAndCheck = [&](const PreparationStep& step)
    {
        reportProgress(step.description);
        auto result = runStep(step);

        if (result.launched && result.exitCode == 0)
            return true;

        prepared.problem = step.description + " failed: " + eacp::Strings::lastNonEmptyLine(result.output);
        return false;
    };

    for (const auto& step: makeCloneAndFetchSteps(request))
    {
        if (!runAndCheck(step))
            return prepared;
    }

    auto head = eacp::Processes::run(
        request.gitExecutable,
        {"-C", getWorkingCopyPath(request), "symbolic-ref", "refs/remotes/origin/HEAD"});
    prepared.defaultBranch = parseDefaultBranchReference(head.output);

    if (prepared.defaultBranch.empty())
    {
        prepared.problem = "could not read origin's default branch";
        return prepared;
    }

    if (!runAndCheck(makeWorktreeStep(request, prepared.defaultBranch)))
        return prepared;

    prepared.worktreePath =
        makeWorktreePath(request.projectsDirectory, request.project, request.taskBranch);
    prepared.hasSucceeded = true;
    return prepared;
}
} // namespace term
