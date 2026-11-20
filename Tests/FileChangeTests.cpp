#include <Multiplexer/FileChangeRules.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace nano;
using namespace term::filechanges;

namespace
{
namespace fs = std::filesystem;

constexpr auto foldersWatchedOnLinux = std::size_t {10};
constexpr auto foldersTypedInto = 12;

// A fresh folder under the system's temporary folder, deleted afterwards.
struct ScratchFolder
{
    explicit ScratchFolder(const std::string& name)
        : path(fs::temp_directory_path()
               / (name + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        fs::create_directories(path);
    }

    ~ScratchFolder()
    {
        auto error = std::error_code {};
        fs::remove_all(path, error);
    }

    fs::path path;
};
} // namespace

auto tProjectFolderIsTheRepository = test("FileChangeRules/projectFolderIsTheNearestFolderWithGit") = []
{
    const auto home = ScratchFolder {"cowterm-home"};
    const auto project = home.path / "projects" / "Grazer";
    fs::create_directories(project / ".git");
    fs::create_directories(project / "src" / "deeper");

    check(findProjectFolder(project / "src" / "deeper", home.path) == project.string());
    check(findProjectFolder(project, home.path) == project.string());
};

auto tProjectFolderWithoutGit = test("FileChangeRules/projectFolderWithoutGitIsWhereThePaneStarted") = []
{
    const auto home = ScratchFolder {"cowterm-home"};
    const auto notes = home.path / "notes";
    fs::create_directories(notes);

    check(findProjectFolder(notes, home.path) == notes.string());
};

auto tHomeAndRootAreNeverWatched = test("FileChangeRules/homeFolderAndFilesystemRootAreNeverWatched") = []
{
    const auto home = ScratchFolder {"cowterm-home"};
    const auto root = home.path.root_path();

    check(findProjectFolder(home.path, home.path).empty());
    check(findProjectFolder(home.path / "", home.path).empty(), "with a trailing separator");
    check(findProjectFolder(root, home.path).empty());
    check(findProjectFolder(home.path / "missing", home.path).empty());
    check(findProjectFolder({}, home.path).empty());
};

auto tFoldersOrderedByLastInput = test("FileChangeRules/foldersAreOrderedByLastInputOnceEach") = []
{
    const auto ordered =
        orderFoldersByLastInput({{"/a", 10}, {"/b", 30}, {"", 50}, {"/a", 40}, {"/c", 20}});

    check(ordered == std::vector<std::string> {"/a", "/b", "/c"});
};

auto tLinuxWatchesTheTenLastTypedInto = test("FileChangeRules/watchingEachSubfolderTakesTheTenFoldersTypedIntoLast") = []
{
    auto folders = std::vector<std::string> {};

    for (auto index = 0; index < foldersTypedInto; ++index)
        folders.push_back("/projects/" + std::to_string(index));

    const auto chosen = chooseFoldersToWatchOneSubfolderAtATime(folders);

    check(chosen.size() == foldersWatchedOnLinux);
    check(chosen.front() == "/projects/0" && chosen.back() == "/projects/9");
    check(chooseFoldersToWatchOneSubfolderAtATime({"/a"}) == std::vector<std::string> {"/a"});
};

auto tGitIsAlwaysExcluded = test("FileChangeRules/gitIsExcludedEverywhereAndGeneratedFoldersOnlyPerSubfolder") = []
{
    const auto recursive = listExcludedFolderNames(false);
    const auto eachSubfolder = listExcludedFolderNames(true);

    check(recursive == std::vector<std::string> {".git"}, "build output counts where the watch is recursive");

    for (const auto* name: {".git", "node_modules", "build*", "_deps", "target", ".venv"})
        check(std::ranges::find(eachSubfolder, name) != eachSubfolder.end(), name);
};

auto tChangeMapsToItsFolder = test("FileChangeRules/aChangeBelongsToTheInnermostWatchedFolderItIsIn") = []
{
    const auto folders = std::vector<std::string> {"/p/Grazer", "/p/Grazer/vendor/lib", "/p/iac"};

    check(findWatchedFolderOfChange("/p/Grazer/src/a.cpp", folders) == "/p/Grazer");
    check(findWatchedFolderOfChange("/p/Grazer/vendor/lib/b.cpp", folders) == "/p/Grazer/vendor/lib");
    check(findWatchedFolderOfChange("/p/iac", folders) == "/p/iac", "the folder itself");
    check(findWatchedFolderOfChange("/p/GrazerOld/a.cpp", folders).empty(), "a sibling with a longer name");
    check(findWatchedFolderOfChange("/elsewhere/a.cpp", folders).empty());
};
