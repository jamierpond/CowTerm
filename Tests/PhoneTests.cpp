#include "Phone/DiffViewPane.h"
#include "Phone/HeldPane.h"
#include "Phone/LinkWatch.h"
#include "Phone/MirroredShell.h"
#include "Phone/PaneChoice.h"
#include "Phone/PhoneSettings.h"
#include "Phone/ProjectSession.h"
#include "Phone/SessionOpenings.h"
#include "Phone/StoredScreen.h"

#include "Multiplexer/AgeText.h"
#include "Multiplexer/Protocol.h"
#include "Multiplexer/RemoteLocation.h"
#include "Multiplexer/SessionLayout.h"

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/Files.h>

#include <map>
#include <unistd.h>

using namespace nano;
using namespace term;
using namespace term::phone;

namespace
{
constexpr auto storedScreenFileName = "last-screen.json";

EnvironmentReader makeEnvironment(std::map<std::string, std::string> variables)
{
    return [variables = std::move(variables)](const std::string& name)
    {
        if (auto found = variables.find(name); found != variables.end())
            return found->second;

        return std::string {};
    };
}

std::filesystem::path makeScratchDirectory()
{
    const auto name = "cowterm-phone-tests-" + std::to_string(::getpid());
    return std::filesystem::temp_directory_path() / name;
}

// A pane shell with no daemon: the test plays the daemon's part.
class ScriptedShell final : public Shell
{
public:
    bool start(const PtyOptions& options,
               std::function<void(std::string)> onOutputToUse,
               std::function<void()>) override
    {
        startedSize = options.size;
        startedFolder = options.workingDirectory;
        onOutput = std::move(onOutputToUse);
        return true;
    }

    void write(std::string_view) override {}
    void resize(const PtySize& size) override { resizedTo = size; }
    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }
    void terminate() override {}
    void detach() override {}

    std::function<void(std::string)> onOutput;
    PtySize startedSize;
    std::string startedFolder;
    PtySize resizedTo;
};

std::string readFirstRow(const TerminalModel& model)
{
    auto text = std::string {};

    constexpr auto firstNonAscii = char32_t {0x80};

    for (const auto& cell: model.screen.lineAt(0, 0))
    {
        const auto codepoint = cell.codepoint();
        const auto isPrintedAscii = codepoint > U' ' && codepoint < firstNonAscii;

        if (isPrintedAscii)
        {
            const auto asciiCharacter = static_cast<char>(codepoint);
            text += asciiCharacter;
        }
    }

    return text;
}
} // namespace

auto settingsComeFromLaunchArguments = test("PhoneSettings/comeFromLaunchArguments") = []
{
    const auto settings = parsePhoneSettings(
        {"--destination", "jamiepond@macprorack", "--pane", "pane-1", "--agent-socket", "/tmp/agent"},
        makeEnvironment({}),
        "/data/last-screen.json");

    check(settings.has_value());
    check(settings->destination.host == "macprorack");
    check(settings->explicitPaneId == "pane-1");
    check(settings->credentials.agentSocketPath == "/tmp/agent");
    check(settings->storedScreenPath == "/data/last-screen.json");
};

auto settingsFallBackToTheEnvironment = test("PhoneSettings/fallBackToTheEnvironment") = []
{
    const auto settings = parsePhoneSettings(
        {"--pane", "from-arguments"},
        makeEnvironment({{"COWTERM_DESTINATION", "jamie@tamby"},
                         {"COWTERM_PANE", "from-environment"},
                         {"COWTERM_KEY_FILE", "/keys/id_ecdsa"}}),
        {});

    check(settings.has_value());
    check(settings->destination.user == "jamie");
    check(settings->explicitPaneId == "from-arguments");
    check(settings->credentials.privateKeyPath == "/keys/id_ecdsa");
};

auto settingsNeedADestination = test("PhoneSettings/needADestination") = []
{
    check(!parsePhoneSettings({}, makeEnvironment({}), {}).has_value());
    check(!parsePhoneSettings({"--destination", "tamby"}, makeEnvironment({}), {}).has_value());
};

auto pairedHostsBecomeADestinationList = test("PhoneSettings/pairedHostsBecomeADestinationList") = []
{
    check(joinPairedHosts("jamiepond@big-cow\n jamiepond@tamby \n\njamiepond@macprorack\n")
          == "jamiepond@big-cow,jamiepond@tamby,jamiepond@macprorack");
    check(joinPairedHosts("").empty());
};

auto pairedHostWithAnAddressKeepsItsName = test("PhoneSettings/pairedHostWithAnAddressKeepsItsName") = []
{
    const auto hosts = joinPairedHosts("jamiepond@tamby=jamies-mac-mini.tail8acf23.ts.net\njamiepond@macprorack\n");
    const auto settings = parsePhoneSettings({"--destination", hosts}, makeEnvironment({}), {});

    check(settings && settings->overviewDestinations.size() == 2);

    if (settings && settings->overviewDestinations.size() == 2)
    {
        check(getHostName(settings->overviewDestinations[0]) == "tamby");
        check(getAddressToDial(settings->overviewDestinations[0]) == "jamies-mac-mini.tail8acf23.ts.net");
        check(getAddressToDial(settings->overviewDestinations[1]) == "macprorack");
    }
};

auto hostIsNamedWithoutItsUser = test("PhoneSettings/hostIsNamedWithoutItsUser") = []
{
    const auto settings = parsePhoneSettings({"--destination", "jamiepond@macprorack"}, makeEnvironment({}), {});

    check(settings && getHostName(settings->destination) == "macprorack", "the name the Mac's ssh alias gives it");
};

auto lastInputAnswerNamesThePane = test("PaneChoice/lastInputAnswerNamesThePane") = []
{
    constexpr auto typedAt = std::int64_t {1794906370945};
    const auto answer = parseLastInput("tamby", "last-input abc " + std::to_string(typedAt));

    check(answer && answer->pane == PaneOnHostId {"tamby", "abc"});
    check(answer && answer->millisecondsSinceEpoch == typedAt);
    check(!parseLastInput("tamby", "last-input").has_value());
    check(!parseLastInput("tamby", "info\nabc").has_value());
};

auto latestTypingAcrossHostsWins =
    test("PaneChoice/latestTypingAcrossHostsWins") = []
{
    constexpr auto earlier = std::int64_t {1000};
    constexpr auto later = std::int64_t {2000};
    const auto answers = std::vector<LastInput> {{{"tamby", "a"}, earlier},
                                                 {{"macprorack", "b"}, later}};
    const auto listsEverything = [](const PaneOnHostId&) { return true; };

    check(chooseLaunchPane(answers, PaneOnHostId {"tamby", "c"}, listsEverything)
          == std::optional {PaneOnHostId {"macprorack", "b"}});
};

auto lastShownPaneOnlyWhenStillListed =
    test("PaneChoice/lastShownPaneOnlyWhenStillListed") = []
{
    const auto shownLastTime = PaneOnHostId {"tamby", "c"};
    const auto listsEverything = [](const PaneOnHostId&) { return true; };
    const auto listsNothing = [](const PaneOnHostId&) { return false; };

    check(chooseLaunchPane({}, shownLastTime, listsEverything)
          == std::optional {shownLastTime});
    check(!chooseLaunchPane({}, shownLastTime, listsNothing).has_value());
    check(!chooseLaunchPane({}, std::nullopt, listsEverything).has_value(),
          "never the first listed pane");
};

auto sessionPanesKeepTheirOrder = test("PaneChoice/sessionPanesKeepTheirOrder") = []
{
    const auto listed = std::vector<ListedPane> {{"a", "tamby/x"}, {"b", "tamby/y"}, {"c", "tamby/x"}};

    check(listPanesOfSession(listed, "tamby/x") == std::vector<std::string> {"a", "c"});
};

auto quietLinkStallsThenRedials = test("LinkWatch/quietLinkStallsThenRedials") = []
{
    constexpr auto heardAt = Milliseconds {1000};
    constexpr auto shortlyAfter = Milliseconds {2000};
    constexpr auto afterStall = Milliseconds {5000};
    constexpr auto afterRedialSilence = Milliseconds {10000};
    auto watch = LinkWatch {};
    watch.noteBytesHeard(heardAt);
    const auto connected = RemoteHost::Status::Connected;

    check(watch.classify(connected, shortlyAfter) == PhoneLinkState::Live);
    check(watch.classify(connected, afterStall) == PhoneLinkState::Stalled);
    check(!watch.shouldReconnect(connected, afterStall));
    check(watch.shouldReconnect(connected, afterRedialSilence));
};

auto wakingRedialsALinkThatStaysQuiet = test("LinkWatch/wakingRedialsALinkThatStaysQuiet") = []
{
    constexpr auto wokeAt = Milliseconds {1000};
    constexpr auto beforeTheAnswerWait = Milliseconds {2000};
    constexpr auto afterTheAnswerWait = Milliseconds {3500};
    auto watch = LinkWatch {};
    watch.noteBytesHeard(wokeAt);
    watch.noteWokeUp(wokeAt);
    const auto connected = RemoteHost::Status::Connected;

    check(!watch.shouldReconnect(connected, beforeTheAnswerWait));
    check(watch.shouldReconnect(connected, afterTheAnswerWait));

    watch.noteBytesHeard(beforeTheAnswerWait);
    check(!watch.shouldReconnect(connected, afterTheAnswerWait));
};

auto wakingSkipsTheBackoffOfAnUnreachableHost = test("LinkWatch/wakingSkipsTheBackoffOfAnUnreachableHost") = []
{
    constexpr auto now = Milliseconds {1000};
    auto watch = LinkWatch {};
    const auto unreachable = RemoteHost::Status::Unreachable;

    check(!watch.shouldReconnect(unreachable, now));
    watch.noteWokeUp(now);
    check(watch.shouldReconnect(unreachable, now));
    watch.noteReconnecting(now);
    check(!watch.shouldReconnect(unreachable, now));
    check(watch.classify(unreachable, now) == PhoneLinkState::Unreachable);
};

auto storedScreenRoundTrips = test("StoredScreen/roundTrips") = []
{
    constexpr auto columns = 20;
    constexpr auto rows = 5;
    constexpr auto savedAt = std::int64_t {1794906370945};
    auto theme = Theme {};
    auto model = TerminalModel {columns, rows, theme};
    model.feed("hello\r\nworld");

    const auto path = makeScratchDirectory() / storedScreenFileName;
    const auto saved = StoredScreen {"macprorack", "pane-1", "macprorack/tmp", savedAt, model.captureState()};
    check(saveStoredScreen(path, saved));

    const auto loaded = loadStoredScreen(path);
    std::filesystem::remove_all(path.parent_path());
    check(loaded.has_value());
    check(loaded->host == saved.host);
    check(loaded->paneId == saved.paneId);
    check(loaded->sessionName == saved.sessionName);
    check(loaded->savedAtMillisecondsSinceEpoch == savedAt);
    check(serializeTerminalState(loaded->state) == serializeTerminalState(saved.state));
};

auto storedScreenFromBeforeHostNamesLostTheUser = test("StoredScreen/fromBeforeHostNamesLostTheUserIsFound") = []
{
    constexpr auto columns = 20;
    constexpr auto rows = 5;
    auto theme = Theme {};
    auto model = TerminalModel {columns, rows, theme};
    const auto path = makeScratchDirectory() / storedScreenFileName;
    check(saveStoredScreen(path, {"jamiepond@macprorack", "pane-1", "macprorack/tmp", 0, model.captureState()}));

    const auto loaded = loadStoredScreen(path);
    std::filesystem::remove_all(path.parent_path());
    check(loaded && loaded->host == "macprorack");
};

auto heldPaneFromBeforeHostNamesLostTheUser = test("HeldPane/fromBeforeHostNamesLostTheUserIsFound") = []
{
    constexpr auto deskColumns = 150;
    constexpr auto deskRows = 41;
    const auto path = makeScratchDirectory() / "held-pane.json";
    check(saveHeldPane(path, {"jamie@tamby", "pane-held", {deskColumns, deskRows}}));

    const auto loaded = loadHeldPane(path);
    std::filesystem::remove_all(path.parent_path());
    check(loaded && loaded->host == "tamby");
    check(loaded && loaded->paneId == "pane-held");
};

auto missingOrBrokenStoredScreenIsEmpty = test("StoredScreen/missingOrBrokenIsEmpty") = []
{
    const auto directory = makeScratchDirectory();
    const auto path = directory / storedScreenFileName;
    check(!loadStoredScreen(path).has_value());

    std::filesystem::create_directories(directory);
    eacp::Files::writeFile(eacp::FilePath {path.string()}, std::string_view {"{not json"});
    check(!loadStoredScreen(path).has_value());
    std::filesystem::remove_all(directory);
};

auto storedScreenAgeReadsLikeAClock = test("StoredScreen/ageReadsLikeAClock") = []
{
    constexpr auto halfASecond = std::int64_t {500};
    constexpr auto twelveSeconds = std::int64_t {12000};
    constexpr auto fourMinutes = std::int64_t {4 * 60 * 1000};
    constexpr auto threeHours = std::int64_t {3 * 60 * 60 * 1000};
    constexpr auto twoDays = std::int64_t {2 * 24 * 60 * 60 * 1000};

    check(describeStoredScreenAge(halfASecond) == "just now");
    check(describeStoredScreenAge(twelveSeconds) == "12 s ago");
    check(describeStoredScreenAge(fourMinutes) == "4 min ago");
    check(describeStoredScreenAge(threeHours) == "3 h ago");
    check(describeStoredScreenAge(twoDays) == "2 d ago");
};

auto lastUseReadsLikeAPerson = test("StoredScreen/lastUseReadsLikeAPerson") = []
{
    constexpr auto fortySeconds = std::int64_t {40 * 1000};
    constexpr auto twoMinutes = std::int64_t {2 * 60 * 1000};
    constexpr auto threeHours = std::int64_t {3 * 60 * 60 * 1000};
    constexpr auto thirtyHours = std::int64_t {30 * 60 * 60 * 1000};
    constexpr auto fourDays = std::int64_t {4 * 24 * 60 * 60 * 1000};

    check(describeLastUseAge(fortySeconds) == "just now");
    check(describeLastUseAge(twoMinutes) == "2 min ago");
    check(describeLastUseAge(threeHours) == "3 h ago");
    check(describeLastUseAge(thirtyHours) == "yesterday");
    check(describeLastUseAge(fourDays) == "4 d ago");
};

auto mirrorSeesOutputSizeAndStatus = test("MirroredShell/seesOutputSizeAndStatus") = []
{
    constexpr auto startColumns = 30;
    constexpr auto startRows = 6;
    constexpr auto phoneColumns = 49;
    constexpr auto phoneRows = 25;
    auto inner = std::make_unique<ScriptedShell>();
    auto& scripted = *inner;
    auto mirror = std::make_shared<PaneMirror>();
    auto notes = std::vector<std::string> {};
    auto shown = std::string {};
    auto shell = MirroredShell {std::move(inner), mirror, [&](const std::string& note) { notes.push_back(note); }};

    shell.start({{startColumns, startRows}, {}, {}, {}}, [&](std::string bytes) { shown += bytes; }, [] {});
    check(mirror->model.screen.columns() == startColumns);
    check(!mirror->hasContent);

    scripted.onOutput("hi");
    check(shown == "hi");
    check(readFirstRow(mirror->model) == "hi");
    check(mirror->hasContent);

    shell.resize({phoneColumns, phoneRows});
    check(mirror->model.screen.columns() == phoneColumns);
    check(scripted.resizedTo.cols == phoneColumns);

    scripted.onStatusChanged("on Mac, 150 columns; type here to take it back");
    check(notes.size() == 1);
};

auto openingsAreKeptAndDecay = test("SessionOpenings/openingsAreKeptAcrossLaunchesAndDecay") = []
{
    constexpr auto now = std::int64_t {1'800'000'000'000};
    constexpr auto aMonth = std::int64_t {30} * 24 * 60 * 60 * 1000;
    const auto path = std::filesystem::temp_directory_path()
                      / ("cowterm-session-openings-" + std::to_string(::getpid()) + ".json");
    std::filesystem::remove(path);

    {
        auto openings = SessionOpenings {path};
        openings.recordOpening("tamby/CowTerm", now);
        openings.recordOpening("tamby/CowTerm", now);
        openings.recordOpening("tamby/Grazer", now);
    }

    const auto reopened = SessionOpenings {path};
    check(reopened.getUsage("tamby/CowTerm", now) > reopened.getUsage("tamby/Grazer", now));
    check(reopened.getUsage("tamby/never", now) == 0.0);
    check(reopened.getLastOpeningMilliseconds("tamby/Grazer") == std::optional<std::int64_t> {now});
    check(!reopened.getLastOpeningMilliseconds("tamby/never").has_value());
    check(reopened.getUsage("tamby/CowTerm", now + aMonth) < reopened.getUsage("tamby/CowTerm", now),
          "older openings count for less");
    std::filesystem::remove(path);
};

auto openingsFromBeforeHostNamesLostTheUserCount =
    test("SessionOpenings/openingsFromBeforeHostNamesLostTheUserCountUnderTheNewName") = []
{
    constexpr auto earlier = std::int64_t {1'800'000'000'000};
    constexpr auto later = earlier + 1;
    const auto path = std::filesystem::temp_directory_path()
                      / ("cowterm-session-openings-migration-" + std::to_string(::getpid()) + ".json");
    std::filesystem::remove(path);

    {
        auto openings = SessionOpenings {path};
        openings.recordOpening("jamiepond@macprorack/macprorack/iac", earlier);
        openings.recordOpening("jamie@tamby/CowTerm", later);
        openings.recordOpening("tamby/CowTerm", earlier);
        openings.recordOpening("big-cow/Grazer", earlier);
    }

    const auto reopened = SessionOpenings {path};
    std::filesystem::remove(path);
    check(reopened.getLastOpeningMilliseconds("macprorack/macprorack/iac") == std::optional<std::int64_t> {earlier});
    check(!reopened.getLastOpeningMilliseconds("jamiepond@macprorack/macprorack/iac").has_value());
    check(reopened.getLastOpeningMilliseconds("tamby/CowTerm") == std::optional<std::int64_t> {later},
          "a session recorded under both names keeps the one opened last");
    check(reopened.getLastOpeningMilliseconds("big-cow/Grazer") == std::optional<std::int64_t> {earlier});
};

namespace
{
constexpr auto repositoryFolder = "/tmp/cowterm-dv-repo";
constexpr auto someRandomNumber = std::uint64_t {0xabcdef};
constexpr auto anotherRandomNumber = std::uint64_t {0x123456};
} // namespace

auto diffViewRunsInTheShownPanesFolder = test("DiffViewPane/runsInTheShownPanesFolder") = []
{
    const auto pane = makeDiffViewPane("macprorack", repositoryFolder, someRandomNumber);

    check(pane && pane->host == "macprorack");
    check(pane && pane->workingDirectory == repositoryFolder);
    check(pane && pane->paneId == "phone-dv-0000000000abcdef");
    check(pane && isDiffViewPaneId(pane->paneId));
    check(!makeDiffViewPane("macprorack", {}, someRandomNumber), "no folder known yet, no dv");
};

auto diffViewPanesAreLeftOutOfLists = test("DiffViewPane/panesAreLeftOutOfLists") = []
{
    const auto listed = std::vector<std::string> {"pane-a", makeDiffViewPaneId(someRandomNumber), "pane-b"};
    check(removeDiffViewPaneIds(listed) == std::vector<std::string> {"pane-a", "pane-b"});
};

auto diffViewSaysWhyItEnded = test("DiffViewPane/saysWhyItEnded") = []
{
    const auto outsideARepository =
        std::string {"diffview: "} + repositoryFolder + " is not inside a git work tree\n";

    const auto cutAcrossRows = std::string {"diffview: "} + repositoryFolder + " is no\nt inside a git work tree\n";

    check(describeDiffViewFailure(outsideARepository, repositoryFolder)
          == std::string {repositoryFolder} + " is not in a git repository.");
    check(describeDiffViewFailure(cutAcrossRows, repositoryFolder)
          == std::string {repositoryFolder} + " is not in a git repository.");
    const auto brokenWhereTheRowEnds = std::string {"diffview: "} + repositoryFolder + " is no\nt inside a git work tree\n";
    check(describeDiffViewFailure(brokenWhereTheRowEnds, repositoryFolder)
          == std::string {repositoryFolder} + " is not in a git repository.");
    check(describeDiffViewFailure("\n  \n", repositoryFolder).empty(), "a blank screen is dv quitting");
    check(describeDiffViewFailure("fatal: bad object\n", repositoryFolder) == "dv stopped: fatal: bad object");
    check(describeDiffViewNotInstalled({"dv", {}}, "macprorack")
          == "dv is not installed on macprorack (not found on its PATH).");
    check(describeDiffViewNotInstalled({"dv", "/usr/bin:/bin"}, "macprorack")
          == "dv is not installed on macprorack (not found on its PATH, /usr/bin:/bin).");
};

auto temporaryPanesAreKeptUntilKilled = test("DiffViewPane/temporaryPanesAreKeptUntilKilled") = []
{
    const auto path = makeScratchDirectory() / "temporary-panes.json";
    const auto first = TemporaryPane {"macprorack", makeDiffViewPaneId(someRandomNumber)};
    const auto second = TemporaryPane {"big-cow", makeDiffViewPaneId(anotherRandomNumber)};

    check(loadTemporaryPanes(path).empty());
    check(addTemporaryPane(path, first));
    check(addTemporaryPane(path, second));
    check(loadTemporaryPanes(path) == std::vector<TemporaryPane> {first, second});

    removeTemporaryPane(path, first);
    check(loadTemporaryPanes(path) == std::vector<TemporaryPane> {second});

    removeTemporaryPane(path, second);
    check(!std::filesystem::exists(path), "no panes left, no file");
};

auto temporaryPanesFromBeforeHostNamesLostTheUser = test("DiffViewPane/temporaryPanesFromBeforeHostNamesLostTheUserAreFound") = []
{
    const auto path = makeScratchDirectory() / "temporary-panes.json";
    const auto paneId = makeDiffViewPaneId(someRandomNumber);
    check(addTemporaryPane(path, {"jamiepond@macprorack", paneId}));
    check(loadTemporaryPanes(path) == std::vector<TemporaryPane> {{"macprorack", paneId}});

    removeTemporaryPane(path, {"macprorack", paneId});
    check(!std::filesystem::exists(path), "killed under its new name, the note is gone");
};

auto diffViewShellStartsOnlyDiffViewInItsFolder = test("DiffViewPane/shellStartsOnlyDiffViewInItsFolder") = []
{
    const auto pane = *makeDiffViewPane("macprorack", repositoryFolder, someRandomNumber);
    const auto startWith = [&](const std::string& command, const std::string& folder)
    {
        auto inner = std::make_unique<ScriptedShell>();
        auto& scripted = *inner;
        auto shell = DiffViewShell {std::move(inner), pane};
        const auto started = shell.start({{}, folder, command, {}}, [](std::string) {}, [] {});
        return started && scripted.onOutput != nullptr;
    };

    check(startWith(diffViewCommand, repositoryFolder));
    check(!startWith("bash", repositoryFolder), "no other command");
    check(!startWith({}, repositoryFolder), "no interactive shell");
    check(!startWith(diffViewCommand, "/"), "no other folder");
};

namespace
{
constexpr auto scannedHost = "macprorack";
constexpr auto scannedProjectFolder = "/home/jamiepond/projects/iac";
constexpr auto folderNotScanned = "/home/jamiepond/secrets";

std::vector<ProjectDir> makeLastScan()
{
    return {{remote::makeRemoteProjectDir(scannedHost, scannedProjectFolder), "iac"}};
}
} // namespace

auto projectSessionIsNamedAsTheMacNamesIt = test("ProjectSession/isNamedAsTheMacNamesTheProjectsSession") = []
{
    const auto session = makeProjectSession(scannedHost, makeLastScan(), scannedProjectFolder, someRandomNumber);

    check(session && session->sessionName == "macprorack/iac");
    check(session && session->folder == scannedProjectFolder);
    check(session && session->paneId == "0000000000abcdef");
};

auto projectSessionOnlyForAScannedFolder = test("ProjectSession/onlyForAFolderTheHostsScanListed") = []
{
    check(!makeProjectSession(scannedHost, makeLastScan(), folderNotScanned, someRandomNumber));
    check(!makeProjectSession("jamie@tamby", makeLastScan(), scannedProjectFolder, someRandomNumber),
          "the folder must be in that host's scan");
    check(!makeProjectSession(scannedHost, {}, scannedProjectFolder, someRandomNumber), "no scan yet, no session");
};

auto projectShellStartsOnlyTheLoginShellInItsFolder = test("ProjectSession/shellStartsOnlyTheLoginShellInItsFolder") = []
{
    const auto session = *makeProjectSession(scannedHost, makeLastScan(), scannedProjectFolder, someRandomNumber);
    auto startedFolder = std::string {};
    const auto startWith = [&](const std::string& command, const std::string& folder)
    {
        auto inner = std::make_unique<ScriptedShell>();
        auto& scripted = *inner;
        auto shell = ProjectShell {std::move(inner), session};
        const auto started = shell.start({{}, folder, command, {}}, [](std::string) {}, [] {});
        startedFolder = scripted.startedFolder;
        return started && scripted.onOutput != nullptr;
    };

    check(startWith({}, {}));
    check(startedFolder == scannedProjectFolder, "a view that names no folder gets the project's");
    check(startWith({}, scannedProjectFolder));
    check(!startWith("bash -c 'rm -rf ~'", scannedProjectFolder), "no command");
    check(!startWith({}, folderNotScanned), "no other folder");
};

// What the Mac does with the layout the phone leaves on macprorack's daemon:
// RemoteHost::readLayouts parses the message, SessionManager::adoptRemoteSession
// files the session under normalizedDir(projectDir), and the palette lists a
// scanned project (RemoteHost::readScanResult names it "<host>:<folder>") only
// while no session is filed under that path. So the phone's session must be
// filed exactly under the Mac's project row, under the Mac's name for it.
auto projectSessionLayoutIsTheMacsSessionForTheProject =
    test("ProjectSession/layoutIsAdoptedByTheMacAsTheProjectsOneSession") = []
{
    const auto phoneSettings = parsePhoneSettings({"--destination", "jamiepond@macprorack"}, makeEnvironment({}), {});
    const auto phoneHost = getHostName(phoneSettings->destination);
    const auto phoneScan = std::vector<ProjectDir> {{remote::makeRemoteProjectDir(phoneHost, scannedProjectFolder), "iac"}};
    const auto session = *makeProjectSession(phoneHost, phoneScan, scannedProjectFolder, someRandomNumber);

    const auto message = proto::parse(makeLayoutMessage(session.makeLayout()));
    auto adopted = SavedSession {};
    Miro::fromJSONString(adopted, message.payload);

    constexpr auto macSshAlias = "macprorack";
    const auto macProjectRowPath = remote::makeRemoteProjectDir(macSshAlias, scannedProjectFolder);

    check(message.verb == "layout");
    check(proto::decodeArgument(message.args.at(0)) == "macprorack/iac");
    check(normalizedDir(adopted.projectDir) == macProjectRowPath, "the Mac hides the project row behind the session");
    check(adopted.name == getSessionNameForProject(macProjectRowPath), "the name the Mac gives the project's session");
    check(adopted.panes.size() == 1 && adopted.panes.front().shellId == session.paneId, "the phone's pane");
    check(adopted.panes.front().cwd == scannedProjectFolder);
};

auto projectShellPublishesItsLayoutOnlyOnceStarted = test("ProjectSession/shellSaysWhenItsLoginShellStarted") = []
{
    const auto session = *makeProjectSession(scannedHost, makeLastScan(), scannedProjectFolder, someRandomNumber);
    auto startedCount = 0;
    auto shell = ProjectShell {std::make_unique<ScriptedShell>(), session};
    shell.onLoginShellStarted = [&] { ++startedCount; };

    check(!shell.start({{}, {}, "bash", {}}, [](std::string) {}, [] {}));
    check(startedCount == 0, "nothing started, nothing published");
    check(shell.start({{}, {}, {}, {}}, [](std::string) {}, [] {}));
    check(startedCount == 1);
};
