#include "GitHubPages.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::github;

namespace
{
constexpr auto cowTermPage = "https://github.com/jamierpond/CowTerm";

RepositoryFacts makeFactsOnBranch(const std::string& branch)
{
    auto facts = RepositoryFacts {};
    facts.remoteUrl = "git@github.com:jamierpond/CowTerm.git";
    facts.branch = branch;
    facts.defaultBranch = "main";
    return facts;
}
} // namespace

auto scpStyleSshRemoteBecomesPage =
    test("GitHubPages/scpStyleSshRemoteBecomesPage") = []
{
    check(convertRemoteUrlToRepositoryPage("git@github.com:jamierpond/CowTerm.git")
          == cowTermPage);
};

auto sshUrlRemoteBecomesPage = test("GitHubPages/sshUrlRemoteBecomesPage") = []
{
    check(convertRemoteUrlToRepositoryPage(
              "ssh://git@github.com/jamierpond/CowTerm.git")
          == cowTermPage);
};

auto httpsRemoteBecomesPageWithOrWithoutDotGit =
    test("GitHubPages/httpsRemoteBecomesPageWithOrWithoutDotGit") = []
{
    check(
        convertRemoteUrlToRepositoryPage("https://github.com/jamierpond/CowTerm.git")
        == cowTermPage);
    check(convertRemoteUrlToRepositoryPage("https://github.com/jamierpond/CowTerm")
          == cowTermPage);
    check(
        convertRemoteUrlToRepositoryPage("https://github.com/jamierpond/CowTerm/\n")
        == cowTermPage);
};

auto remoteOffGitHubHasNoPage = test("GitHubPages/remoteOffGitHubHasNoPage") = []
{
    check(convertRemoteUrlToRepositoryPage("git@gitlab.com:jamierpond/CowTerm.git")
              .empty());
    check(convertRemoteUrlToRepositoryPage("https://github.com.evil.example/a/b")
              .empty());
    check(convertRemoteUrlToRepositoryPage("https://github.com/onlyowner").empty());
    check(convertRemoteUrlToRepositoryPage("").empty());
};

auto compareUrlNamesDefaultAndBranch =
    test("GitHubPages/compareUrlNamesDefaultAndBranch") = []
{
    check(
        makeCompareUrl(cowTermPage, "main", "jp/open-github")
        == "https://github.com/jamierpond/CowTerm/compare/main...jp/open-github?expand=1");
};

auto compareUrlWithoutDefaultLeavesBaseToGitHub =
    test("GitHubPages/compareUrlWithoutDefaultLeavesBaseToGitHub") = []
{
    check(makeCompareUrl(cowTermPage, "", "fix")
          == "https://github.com/jamierpond/CowTerm/compare/fix?expand=1");
};

auto compareUrlEscapesBranchCharacters =
    test("GitHubPages/compareUrlEscapesBranchCharacters") = []
{
    check(
        makeCompareUrl(cowTermPage, "main", "fix#1 a")
        == "https://github.com/jamierpond/CowTerm/compare/main...fix%231%20a?expand=1");
};

auto outputWithPullRequestIsParsed =
    test("GitHubPages/outputWithPullRequestIsParsed") = []
{
    const auto facts = parseRepositoryQueryOutput(
        "remote=git@github.com:jamierpond/CowTerm.git\n"
        "branch=jp/fix\n"
        "default=origin/main\n"
        "pr=https://github.com/jamierpond/CowTerm/pull/12\n");

    check(facts.problem == FolderProblem::none);
    check(facts.remoteUrl == "git@github.com:jamierpond/CowTerm.git");
    check(facts.branch == "jp/fix");
    check(facts.defaultBranch == "main");
    check(facts.pullRequestUrl == "https://github.com/jamierpond/CowTerm/pull/12");
};

auto outputWithoutGhHasNoPullRequest =
    test("GitHubPages/outputWithoutGhHasNoPullRequest") = []
{
    const auto facts = parseRepositoryQueryOutput(
        "remote=https://github.com/jamierpond/CowTerm.git\r\nbranch=fix\r\ndefault=origin/main\r\n");

    check(facts.branch == "fix");
    check(facts.defaultBranch == "main");
    check(facts.pullRequestUrl.empty());
};

auto pullRequestKeyOpensExistingPullRequest =
    test("GitHubPages/pullRequestKeyOpensExistingPullRequest") = []
{
    const auto facts = parseRepositoryQueryOutput(
        "remote=git@github.com:jamierpond/CowTerm.git\nbranch=fix\ndefault=origin/main\n"
        "pr=https://github.com/jamierpond/CowTerm/pull/12\n");

    check(choosePage(facts, PageKind::pullRequest).url
          == "https://github.com/jamierpond/CowTerm/pull/12");
};

auto pullRequestKeyWithNoPullRequestOpensCompare =
    test("GitHubPages/pullRequestKeyWithNoPullRequestOpensCompare") = []
{
    const auto facts = parseRepositoryQueryOutput(
        "remote=git@github.com:jamierpond/CowTerm.git\nbranch=fix\ndefault=origin/main\npr=\n");

    check(choosePage(facts, PageKind::pullRequest).url
          == "https://github.com/jamierpond/CowTerm/compare/main...fix?expand=1");
};

auto repositoryKeyOpensRepositoryPage =
    test("GitHubPages/repositoryKeyOpensRepositoryPage") = []
{
    const auto choice = choosePage(makeFactsOnBranch(""), PageKind::repository);

    check(choice.url == cowTermPage);
    check(choice.failureReason.empty());
};

auto folderProblemsOpenNothing = test("GitHubPages/folderProblemsOpenNothing") = []
{
    const auto missing = parseRepositoryQueryOutput("problem=folder-missing\n");
    const auto notRepository =
        parseRepositoryQueryOutput("problem=not-a-repository\n");

    check(missing.problem == FolderProblem::folderMissing);
    check(notRepository.problem == FolderProblem::notARepository);
    check(choosePage(missing, PageKind::repository).url.empty());
    check(!choosePage(notRepository, PageKind::repository).failureReason.empty());
};

auto unrecognisedOutputOpensNothing =
    test("GitHubPages/unrecognisedOutputOpensNothing") = []
{
    const auto choice = choosePage(
        parseRepositoryQueryOutput("bash: sh: not found\n"), PageKind::repository);

    check(choice.url.empty());
    check(!choice.failureReason.empty());
};

auto remoteOffGitHubOpensNothing =
    test("GitHubPages/remoteOffGitHubOpensNothing") = []
{
    auto facts = makeFactsOnBranch("fix");
    facts.remoteUrl = "git@gitlab.com:a/b.git";
    const auto choice = choosePage(facts, PageKind::repository);

    check(choice.url.empty());
    check(choice.failureReason.find("gitlab.com") != std::string::npos);
};

auto detachedHeadHasNoPullRequest =
    test("GitHubPages/detachedHeadHasNoPullRequest") = []
{
    const auto choice = choosePage(makeFactsOnBranch(""), PageKind::pullRequest);

    check(choice.url.empty());
    check(!choice.failureReason.empty());
};

auto defaultBranchHasNoPullRequest =
    test("GitHubPages/defaultBranchHasNoPullRequest") = []
{
    const auto choice = choosePage(makeFactsOnBranch("main"), PageKind::pullRequest);

    check(choice.url.empty());
    check(!choice.failureReason.empty());
};

auto scriptQuotesTheFolder = test("GitHubPages/scriptQuotesTheFolder") = []
{
    const auto script =
        makeRepositoryQueryScript({"", "/tmp/it's here"}, PageKind::repository);

    check(script.find("'/tmp/it'\\''s here'") != std::string::npos);
    check(script.find("gh ") == std::string::npos);
};

auto scriptExpandsHomeTilde = test("GitHubPages/scriptExpandsHomeTilde") = []
{
    const auto script =
        makeRepositoryQueryScript({"tamby", "~/projects/x"}, PageKind::pullRequest);

    check(script.find("\"$HOME\"'/projects/x'") != std::string::npos);
    check(script.find("gh pr view") != std::string::npos);
};

auto remoteQueryRunsOverSsh = test("GitHubPages/remoteQueryRunsOverSsh") = []
{
    const auto command =
        makeRepositoryQueryCommand({"tamby", "/x"}, PageKind::repository);

    check(command.front() == "ssh");
    check(command[command.size() - 2] == "tamby");
    check(command.back()
          == makeRepositoryQueryScript({"", "/x"}, PageKind::repository));
};

auto localQueryRunsInShell = test("GitHubPages/localQueryRunsInShell") = []
{
    const auto command =
        makeRepositoryQueryCommand({"", "/x"}, PageKind::repository);

    check(command.front() == "/bin/sh");
    check(command.back()
          == makeRepositoryQueryScript({"", "/x"}, PageKind::repository));
};
