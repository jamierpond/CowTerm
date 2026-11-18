#include "DaemonConversation.h"

#include <Multiplexer/Protocol.h>
#include <PondApp/DataDirectory.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <string>

using namespace nano;
using namespace eacp;
using namespace daemontest;

namespace
{
constexpr auto paneId = "phone-protocol-pane";
constexpr auto otherPaneId = "phone-protocol-other-pane";
constexpr auto sessionName = "Grazer";
constexpr auto desktopColumns = 150;
constexpr auto widerDesktopColumns = 160;
constexpr auto phoneColumns = 49;
constexpr auto oldClientColumns = 100;
constexpr auto rows = 40;
constexpr auto ownerFlagArgument = std::size_t {3};
constexpr auto ownerNameArgument = std::size_t {4};
constexpr auto columnsArgument = std::size_t {1};
constexpr auto sizeOwnerArgumentCountWithoutName = std::size_t {4};
constexpr auto lastInputArgumentCount = std::size_t {2};

std::string toArgument(int value)
{
    return std::to_string(value);
}

// A running daemon under its own instance name, quit and cleaned up after.
struct TestDaemon
{
    explicit TestDaemon(const std::string& testName)
        : instance(makeInstanceName(testName))
        , process(launchDaemon(instance))
    {
    }

    ~TestDaemon()
    {
        auto last = DaemonConversation {instance};

        if (last.waitForConnection())
            last.quitDaemon();

        process.wait();
        std::filesystem::remove_all(pondapp::getPondAppDataDirectory("CowTerm")
                                    / ("panes-" + instance));
    }

    std::string instance;
    Processes::Process process;
};

void introduce(DaemonConversation& client, const std::string& name)
{
    client.messenger.send(term::proto::make(
        "hello",
        {toArgument(term::proto::daemonProtocolVersion), term::proto::encodeArgument(name)}));
    client.waitForMessage("daemon-hello");
}

void openPane(DaemonConversation& client, const std::string& id, int columns)
{
    client.messenger.send(term::proto::make(
        "spawn", {id, toArgument(columns), toArgument(rows), sessionName}, "/tmp"));
}

// Opens a new pane and waits for its shell's first output, so a second
// client's spawn of the same id adopts it rather than racing to create it.
void createPane(DaemonConversation& client, const std::string& id, int columns)
{
    openPane(client, id, columns);
    client.waitForMessage("output");
}

// The pane's width in the daemon, read the way a client from before size
// owners would: by adopting it and reading its state.
int readPaneColumns(const std::string& instance)
{
    auto reader = DaemonConversation {instance};

    if (!reader.waitForConnection())
        return 0;

    openPane(reader, paneId, desktopColumns);
    const auto state = reader.waitForState();
    reader.messenger.send(term::proto::make("release", {paneId}));
    return state ? state->screen.columns : 0;
}

struct ExpectedSizeOwner
{
    int columns = 0;
    bool isReceiverTheOwner = false;
    std::string ownerName;
};

bool isSizeOwnerNotice(const std::optional<term::proto::Message>& notice,
                       const ExpectedSizeOwner& expected)
{
    if (!notice || notice->args.size() < sizeOwnerArgumentCountWithoutName)
        return false;

    const auto expectedFlag = expected.isReceiverTheOwner ? "1" : "0";
    auto receivedName = std::string {};

    if (notice->args.size() > ownerNameArgument)
        receivedName = term::proto::decodeArgument(notice->args[ownerNameArgument]);

    return notice->args[columnsArgument] == toArgument(expected.columns)
           && notice->args[ownerFlagArgument] == expectedFlag && receivedName == expected.ownerName;
}
} // namespace

auto tPhoneClaimTellsEveryWatcher = test("DaemonPhoneProtocol/phoneClaimTellsEveryWatcher") = []
{
    auto daemon = TestDaemon {"daemon-size-owner-test"};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, "my iPhone");

    createPane(mac, paneId, desktopColumns);
    openPane(phone, paneId, phoneColumns);
    check(readPaneColumns(daemon.instance) == desktopColumns, "adopting does not take the size");

    phone.messenger.send(term::proto::make(
        "claim-size", {paneId, toArgument(phoneColumns), toArgument(rows)}));

    check(isSizeOwnerNotice(mac.waitForMessage("size-owner"), {phoneColumns, false, "my iPhone"}),
          mac.listReceivedVerbs());
    check(isSizeOwnerNotice(phone.waitForMessage("size-owner"), {phoneColumns, true, "my iPhone"}));
    check(readPaneColumns(daemon.instance) == phoneColumns);

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tMacResizeWaitsUntilItReclaims = test("DaemonPhoneProtocol/macResizeWaitsUntilItReclaims") = []
{
    auto daemon = TestDaemon {"daemon-reclaim-test"};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, "iPhone");

    createPane(mac, paneId, desktopColumns);
    openPane(phone, paneId, phoneColumns);
    phone.messenger.send(term::proto::make(
        "claim-size", {paneId, toArgument(phoneColumns), toArgument(rows)}));
    mac.waitForMessage("size-owner");
    phone.waitForMessage("size-owner");

    mac.messenger.send(term::proto::make(
        "resize", {paneId, toArgument(widerDesktopColumns), toArgument(rows)}));
    check(readPaneColumns(daemon.instance) == phoneColumns, "the phone still holds the pane");

    mac.messenger.send(term::proto::make("reclaim-sizes"));
    check(isSizeOwnerNotice(mac.waitForMessage("size-owner"), {widerDesktopColumns, true, "Mac"}));
    check(isSizeOwnerNotice(phone.waitForMessage("size-owner"), {widerDesktopColumns, false, "Mac"}));

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tPhoneInTheBackgroundYieldsThePane = test("DaemonPhoneProtocol/phoneInTheBackgroundYieldsThePane") = []
{
    auto daemon = TestDaemon {"daemon-yield-test"};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, "iPhone");

    createPane(mac, paneId, desktopColumns);
    openPane(phone, paneId, phoneColumns);
    phone.messenger.send(term::proto::make(
        "claim-size", {paneId, toArgument(phoneColumns), toArgument(rows)}));
    mac.waitForMessage("size-owner");
    phone.waitForMessage("size-owner");

    phone.messenger.send(term::proto::make("yield-size", {paneId}));
    check(isSizeOwnerNotice(mac.waitForMessage("size-owner"), {desktopColumns, true, "Mac"}));
    check(readPaneColumns(daemon.instance) == desktopColumns, "the Mac has its size back while the phone still watches");

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tSilentPhoneLosesThePane = test("DaemonPhoneProtocol/aPhoneThatGoesSilentLosesThePane") = []
{
    // A Mac showing a pane asks for info about this often; the phone, frozen
    // as iOS suspends it with its link still open, sends nothing at all.
    constexpr auto macInfoInterval = Time::MS {500};
    auto daemon = TestDaemon {"daemon-silent-phone-test"};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, "iPhone");

    createPane(mac, paneId, desktopColumns);
    openPane(phone, paneId, phoneColumns);
    phone.messenger.send(term::proto::make(
        "claim-size", {paneId, toArgument(phoneColumns), toArgument(rows)}));
    mac.waitForMessage("size-owner");
    check(readPaneColumns(daemon.instance) == phoneColumns);

    auto macKeepsAsking = Threads::Timer {[&mac] { mac.messenger.send(term::proto::make("info")); }, macInfoInterval};
    check(isSizeOwnerNotice(mac.waitForMessage("size-owner"), {desktopColumns, true, "Mac"}),
          "the Mac has its size back without the phone saying goodbye");

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tPhoneLeavingHandsThePaneBack = test("DaemonPhoneProtocol/phoneLeavingHandsThePaneBack") = []
{
    auto daemon = TestDaemon {"daemon-owner-leaves-test"};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId, desktopColumns);

    {
        auto phone = DaemonConversation {daemon.instance};
        check(phone.waitForConnection());
        introduce(phone, "iPhone");
        openPane(phone, paneId, phoneColumns);
        phone.messenger.send(term::proto::make(
            "claim-size", {paneId, toArgument(phoneColumns), toArgument(rows)}));
        mac.waitForMessage("size-owner");
    }

    check(isSizeOwnerNotice(mac.waitForMessage("size-owner"), {desktopColumns, true, "Mac"}),
          mac.listReceivedVerbs());

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tOldClientResizeStillWins = test("DaemonPhoneProtocol/oldClientResizeStillWins") = []
{
    auto daemon = TestDaemon {"daemon-old-client-test"};
    auto phone = DaemonConversation {daemon.instance};
    auto oldMac = DaemonConversation {daemon.instance};
    check(phone.waitForConnection() && oldMac.waitForConnection());
    introduce(phone, "iPhone");

    createPane(phone, paneId, phoneColumns);
    openPane(oldMac, paneId, desktopColumns);
    oldMac.messenger.send(term::proto::make(
        "resize", {paneId, toArgument(oldClientColumns), toArgument(rows)}));

    check(isSizeOwnerNotice(phone.waitForMessage("size-owner"), {oldClientColumns, false, ""}));
    check(readPaneColumns(daemon.instance) == oldClientColumns);

    phone.messenger.send(term::proto::make("kill", {paneId}));
};

auto tLastInputNamesThePaneTypedInto = test("DaemonPhoneProtocol/lastInputNamesThePaneTypedInto") = []
{
    auto daemon = TestDaemon {"daemon-last-input-test"};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");

    openPane(mac, paneId, desktopColumns);
    openPane(mac, otherPaneId, desktopColumns);

    mac.messenger.send(term::proto::make("last-input"));
    const auto before = mac.waitForMessage("last-input");
    check(before && before->args.empty(), "nobody has typed yet");

    mac.messenger.send(term::proto::make("write", {paneId, term::proto::userInputMarker}, " "));
    mac.messenger.send(term::proto::make("write", {otherPaneId}, "\x1b[0n"));
    mac.messenger.send(term::proto::make("last-input"));

    const auto after = mac.waitForMessage("last-input");
    check(after && after->args.size() == lastInputArgumentCount, mac.listReceivedVerbs());

    if (after && after->args.size() == lastInputArgumentCount)
    {
        check(after->args[0] == paneId, "a terminal's own reply is not typing");
        check(std::stoll(after->args[1]) > 0);
    }

    mac.messenger.send(term::proto::make("kill", {paneId}));
    mac.messenger.send(term::proto::make("kill", {otherPaneId}));
};

auto tActivityCarriesWhatThePaneSaid = test("DaemonPhoneProtocol/activityCarriesWhatThePaneSaid") = []
{
    constexpr auto activityColumnCount = std::size_t {9};
    auto daemon = TestDaemon {"daemon-activity-test"};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");

    createPane(mac, paneId, desktopColumns);
    mac.messenger.send(term::proto::make("write", {paneId, term::proto::userInputMarker}, " "));
    mac.messenger.send(
        term::proto::make("pane-state", {paneId, "waiting"}, "Claude needs\tyour permission\n"));
    mac.messenger.send(term::proto::make("activity"));

    const auto activity = mac.waitForMessage("activity");
    check(activity.has_value(), mac.listReceivedVerbs());

    if (activity)
    {
        auto columns = std::vector<std::string> {};
        auto start = std::size_t {0};
        const auto line = activity->payload.substr(0, activity->payload.find('\n'));

        while (start <= line.size())
        {
            const auto end = std::min(line.find('\t', start), line.size());
            columns.push_back(line.substr(start, end - start));
            start = end + 1;
        }

        check(columns.size() == activityColumnCount, activity->payload);

        if (columns.size() == activityColumnCount)
        {
            check(columns[0] == paneId);
            check(std::stoll(columns[1]) > 0, "typing is recorded");
            check(std::stoll(columns[2]) > 0, "output is recorded");
            check(columns[3] == "waiting");
            check(columns[5] == "Claude needs your permission ", "the message stays on one line");
            check(columns[6] == "waiting", "nobody viewed the pane, so the question is unread");
            check(std::stoll(columns[7]) > 0);
            check(columns[8] == "Claude needs your permission ", "the unread text stays on one line");
        }
    }

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tLocalSessionsAreKnown = test("DaemonPhoneProtocol/localSessionsAreKnown") = []
{
    auto daemon = TestDaemon {"daemon-local-session-test"};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");

    openPane(mac, paneId, desktopColumns);
    mac.messenger.send(term::proto::make("layout", {sessionName}, "{\"layout\": 1}"));
    mac.messenger.send(term::proto::make("info"));
    mac.messenger.send(term::proto::make("layouts"));

    const auto info = mac.waitForMessage("info");
    check(info && info->payload.find(std::string {"\t"} + sessionName + "\n") != std::string::npos,
          "info names the pane's session");

    const auto layouts = mac.waitForMessage("layouts");
    check(layouts && layouts->payload.find(std::string {"layout "} + sessionName) != std::string::npos,
          "the session's layout is kept");

    mac.messenger.send(term::proto::make("kill", {paneId}));
};
