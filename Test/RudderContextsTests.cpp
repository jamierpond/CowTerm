#include "GitBranch.h"
#include "RudderContexts.h"

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <fstream>
#include <random>

// The context list CowTerm publishes to Rudder: one row per pane, then the
// ctrl+a f places nothing is open in yet.

using namespace nano;
using namespace term;

namespace
{
const auto home = std::string {"/Users/me"};

RudderTab twoPaneTab()
{
    return {"CowTerm",
            "/Users/me/projects/CowTerm",
            {{"a1", "", "zsh", "/Users/me/projects/CowTerm"},
             {"a2", "", "nvim", "/Users/me/projects/CowTerm/Lib"}}};
}

void write(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream {file} << text;
}

// A scratch directory, gone when the test is.
struct TempDir
{
    TempDir()
        : path(std::filesystem::temp_directory_path()
               / ("cowterm-gitbranch-" + std::to_string(std::random_device {}())))
    {
        std::filesystem::create_directories(path);
    }

    ~TempDir() { std::filesystem::remove_all(path); }

    std::filesystem::path path;
};
} // namespace

auto tOneContextPerPane = test("RudderContexts/oneContextPerPane") = []
{
    const auto contexts =
        rudderContexts({{"home", "/Users/me", {{"h1", "", "zsh", "/Users/me"}}},
                        twoPaneTab()},
                       {},
                       home);

    check(contexts.size() == 3);
    check(contexts[0].id == "pane:h1");
    check(contexts[1].id == "pane:a1");
    check(contexts[2].id == "pane:a2");
    check(contexts[1].subtitle == "Tab 2 · pane 1 · CowTerm");
    check(contexts[2].subtitle == "~/projects/CowTerm/Lib · Tab 2 · pane 2 · CowTerm");
    check(contexts[0].open && contexts[1].open, "panes preview freely");
};

auto tTitleCarriesCommand = test("RudderContexts/titleCarriesCommand") = []
{
    const auto contexts = rudderContexts({twoPaneTab()}, {}, home);

    check(contexts[0].title == "~/projects/CowTerm", "a shell at its prompt is its cwd");
    check(contexts[1].title == "nvim", "the cwd is the path, and shown in the subtitle");
};

auto tPathIsTheCwd = test("RudderContexts/pathIsTheCwd") = []
{
    // Claude Code titles the pane after the conversation; Rudder still finds
    // it by the project, through the path.
    auto tab = RudderTab {"main",
                          "/Users/me/projects/Grazer",
                          {{"c1", "2.1.299 · Password autofill and apple keychain", "2.1.299",
                            "/Users/me/projects/Grazer"}}};

    const auto contexts =
        rudderContexts({twoPaneTab(), tab}, {{"/Users/me/projects/tamber-web"}}, home);

    check(contexts[0].path == "/Users/me/projects/CowTerm");
    check(contexts[1].path == "/Users/me/projects/CowTerm/Lib");
    check(contexts[2].title == "2.1.299 · Password autofill and apple keychain");
    check(contexts[2].subtitle == "~/projects/Grazer · Tab 2 · pane 1 · main");
    check(contexts[2].path == "/Users/me/projects/Grazer");
    check(contexts[3].path == "/Users/me/projects/tamber-web", "places carry theirs too");
};

auto tFocusedPaneMarked = test("RudderContexts/focusedPaneMarked") = []
{
    auto tab = twoPaneTab();
    tab.panes[1].focused = true;

    const auto contexts = rudderContexts({tab}, {{"/Users/me/projects/x"}}, home);

    check(!contexts[0].focused);
    check(contexts[1].focused, "Esc in Rudder switches back to it");
    check(!contexts[2].focused);
};

auto tTitleSequenceWins = test("RudderContexts/titleSequenceWins") = []
{
    auto tab = twoPaneTab();
    tab.panes[0].title = "~/projects/CowTerm";
    tab.panes[1].title = "nvim Main.cpp";

    const auto contexts = rudderContexts({tab}, {}, home);

    check(contexts[0].title == "~/projects/CowTerm", "no repeated cwd");
    check(contexts[1].title == "nvim Main.cpp", "no repeated command");
};

auto tCommandJoinsAnUnrelatedTitle = test("RudderContexts/commandJoinsTitle") = []
{
    auto tab = twoPaneTab();
    tab.panes[1].command = "htop";
    tab.panes[1].title = "load";

    check(rudderContexts({tab}, {}, home)[1].title == "htop · load");
};

auto tStatusGlyphIsDropped = test("RudderContexts/statusGlyphIsDropped") = []
{
    auto tab = twoPaneTab();
    tab.panes[1].command = "claude";
    tab.panes[1].title = "◐ Fix the build";

    auto spinning = rudderContexts({tab}, {}, home);
    tab.panes[1].title = "◑ Fix the build";

    check(spinning[1].title == "claude · Fix the build");
    check(spinning == rudderContexts({tab}, {}, home), "a spinner frame isn't a change");

    tab.panes[1].title = "~/x y";
    check(rudderContexts({tab}, {}, home)[1].title.starts_with("claude · ~/x y"));
};

auto tHomePrefixIsWholeComponent = test("RudderContexts/homePrefixIsWholeComponent") = []
{
    const auto contexts =
        rudderContexts({{"x", "x", {{"s", "", "zsh", "/Users/meg/src"}}}}, {}, home);

    check(contexts[0].title == "/Users/meg/src");
};

auto tPlacesAfterPanes = test("RudderContexts/placesAfterPanes") = []
{
    const auto contexts = rudderContexts(
        {twoPaneTab()},
        {{"/Users/me/projects"}, {"/Users/me/projects/tamber-web"}, {"/Users/me/projects/CowTerm"}},
        home);

    check(contexts.size() == 4, "the open tab's dir is covered by its panes");
    check(contexts[2].id == "place:/Users/me/projects");
    check(contexts[3].id == "place:/Users/me/projects/tamber-web");
    check(contexts[3].title == "tamber-web");
    check(contexts[3].subtitle == "~/projects/tamber-web");
    check(!contexts[3].open, "activating a place creates a session: no preview");
};

auto tPaneCwdCoversPlace = test("RudderContexts/paneCwdCoversPlace") = []
{
    const auto contexts =
        rudderContexts({twoPaneTab()}, {{"/Users/me/projects/CowTerm/Lib"}}, home);

    check(contexts.size() == 2);
};

auto tParsesIds = test("RudderContexts/parsesIds") = []
{
    auto pane = parseRudderContextId("pane:abc");
    auto place = parseRudderContextId("place:/Users/me/x");

    check(pane && pane->kind == RudderTarget::Kind::Pane && pane->value == "abc");
    check(place && place->kind == RudderTarget::Kind::Place
          && place->value == "/Users/me/x");
    check(!parseRudderContextId("pane:"));
    check(!parseRudderContextId("3"));
};

auto tWindowsShellIsItsCwd = test("RudderContexts/windowsShellIsItsCwd") = []
{
    const auto contexts =
        rudderContexts({{"w", "C:/Users/me/src", {{"w1", "", "pwsh", "C:/Users/me/src"}}}},
                       {},
                       "C:/Users/me");

    check(contexts[0].title == "~/src", "pwsh at its prompt is its cwd, compacted");
};

auto tBranchLeadsTheTitle = test("RudderContexts/branchLeadsTheTitle") = []
{
    auto tab = RudderTab {"CowTerm-rudder-branch",
                          "/Users/me/projects/CowTerm-rudder-branch",
                          {{"w1", "", "zsh", "/Users/me/projects/CowTerm-rudder-branch"},
                           {"w2", "◐ Fix the build", "claude",
                            "/Users/me/projects/CowTerm-rudder-branch/Lib"}}};
    tab.panes[0].branch = "rudder-branch";
    tab.panes[1].branch = "rudder-branch";

    const auto contexts = rudderContexts({tab}, {}, home);

    check(contexts[0].title == "rudder-branch", "the branch, not the worktree dir");
    check(contexts[0].subtitle
          == "~/projects/CowTerm-rudder-branch · Tab 1 · pane 1 · CowTerm-rudder-branch");
    check(contexts[0].branch == "rudder-branch");
    check(contexts[1].title == "rudder-branch · claude · Fix the build");
    check(contexts[1].branch == "rudder-branch");
};

auto tBranchNotRepeated = test("RudderContexts/branchNotRepeated") = []
{
    auto tab = twoPaneTab();
    tab.panes[0].branch = "main";
    tab.panes[0].title = "~/projects/CowTerm";
    tab.panes[1].branch = "main";
    tab.panes[1].title = "main · nvim";

    const auto contexts = rudderContexts({tab}, {}, home);

    check(contexts[0].title == "main", "a shell titled with its cwd is just the branch");
    check(contexts[0].subtitle == "~/projects/CowTerm · Tab 1 · pane 1 · CowTerm",
          "the cwd moves to the subtitle");
    check(contexts[1].title == "main · nvim", "already leads with it");
};

auto tNoBranchNoChange = test("RudderContexts/noBranchNoChange") = []
{
    const auto contexts = rudderContexts({twoPaneTab()}, {}, home);

    check(contexts[0].branch.empty() && contexts[1].branch.empty());
    check(contexts[0].title == "~/projects/CowTerm");
    check(contexts[1].title == "nvim");
};

auto tPlacesCarryBranch = test("RudderContexts/placesCarryBranch") = []
{
    const auto contexts = rudderContexts(
        {}, {{"/Users/me/projects/tamber-web", "develop"}, {"/Users/me/notes"}}, home);

    check(contexts[0].branch == "develop");
    check(contexts[0].title == "tamber-web", "a place is still named for its folder");
    check(contexts[1].branch.empty());
};

auto tGitBranchReadsHead = test("GitBranch/readsHead") = []
{
    const auto dir = TempDir {};
    const auto repo = dir.path / "repo";
    write(repo / ".git" / "HEAD", "ref: refs/heads/feature/thing\n");
    std::filesystem::create_directories(repo / "src" / "deep");

    check(gitBranch(repo.string()) == "feature/thing");
    check(gitBranch((repo / "src" / "deep").string()) == "feature/thing", "from a subdir");
    check(gitBranch(dir.path.string()).empty(), "outside the repo");
    check(gitBranch("").empty());
};

auto tGitBranchWorktree = test("GitBranch/worktree") = []
{
    const auto dir = TempDir {};
    write(dir.path / "repo" / ".git" / "worktrees" / "repo-x" / "HEAD",
          "ref: refs/heads/x\n");
    write(dir.path / "repo-x" / ".git", "gitdir: ../repo/.git/worktrees/repo-x\n");
    write(dir.path / "abs" / ".git",
          "gitdir: " + (dir.path / "repo" / ".git" / "worktrees" / "repo-x").string() + "\n");

    check(gitBranch((dir.path / "repo-x").string()) == "x", "relative gitdir");
    check(gitBranch((dir.path / "abs").string()) == "x", "absolute gitdir");
};

auto tGitBranchDetached = test("GitBranch/detached") = []
{
    const auto dir = TempDir {};
    write(dir.path / ".git" / "HEAD", "3e45fcc0a1b2c3d4e5f60718293a4b5c6d7e8f90\n");

    check(gitBranch(dir.path.string()).empty());
};

auto tGitBranchesFollowCheckout = test("GitBranch/cacheFollowsCheckout") = []
{
    const auto dir = TempDir {};
    auto branches = GitBranches {};
    std::filesystem::create_directories(dir.path / "repo");

    check(branches.of((dir.path / "repo").string()).empty(), "not a repo yet");

    write(dir.path / "repo" / ".git" / "HEAD", "ref: refs/heads/main\n");
    check(branches.of((dir.path / "repo").string()) == "main", "git init is noticed");

    write(dir.path / "repo" / ".git" / "HEAD", "ref: refs/heads/other\n");
    check(branches.of((dir.path / "repo").string()) == "other", "a checkout is noticed");

    std::filesystem::remove_all(dir.path / "repo" / ".git");
    check(branches.of((dir.path / "repo").string()).empty(), "nor is a removed repo");
};
