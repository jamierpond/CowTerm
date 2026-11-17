#include <Multiplexer/DaemonClient.h>
#include <Multiplexer/Protocol.h>
#include <Emulator/TermState.h>

#include <NanoTest/NanoTest.h>

#include <memory>
#include <string>
#include <vector>

using namespace nano;
using namespace term;

namespace
{
constexpr auto paneId = "client-size-owner-pane";
constexpr auto phoneColumns = 49;
constexpr auto rows = 25;

// A link that is always up and keeps what the client sends.
class RecordingDaemonLink final : public DaemonLink
{
public:
    explicit RecordingDaemonLink(std::vector<std::string>& sentToUse)
        : sent(sentToUse)
    {
    }

    bool isConnected() const override { return true; }
    void send(const std::string& message) override { sent.push_back(message); }

    void deliver(const std::string& message) { onMessage(message); }

private:
    std::vector<std::string>& sent;
};

struct ClientWithRecordedLink
{
    ClientWithRecordedLink()
    {
        auto recordingLink = std::make_unique<RecordingDaemonLink>(sent);
        link = recordingLink.get();
        client = std::make_unique<DaemonClient>(std::move(recordingLink), true);
    }

    bool hasSent(const std::string& message) const
    {
        return std::ranges::find(sent, message) != sent.end();
    }

    std::vector<std::string> sent;
    RecordingDaemonLink* link = nullptr;
    std::unique_ptr<DaemonClient> client;
};

std::string makeSizeOwnerMessage(bool isThisClientTheOwner, const std::string& ownerName)
{
    return proto::make("size-owner",
                       {paneId,
                        std::to_string(phoneColumns),
                        std::to_string(rows),
                        isThisClientTheOwner ? "1" : "0",
                        proto::encodeArgument(ownerName)});
}
} // namespace

auto tClientIntroducesItself = test("DaemonClientSizeOwner/clientIntroducesItself") = []
{
    DaemonClient::setClientName("my Mac");
    auto recorded = ClientWithRecordedLink {};

    check(recorded.hasSent(proto::make(
        "hello", {std::to_string(proto::daemonProtocolVersion), "my%20Mac"})));
    DaemonClient::setClientName({});
};

auto tTypingIsMarkedAsInput = test("DaemonClientSizeOwner/typingIsMarkedAsInput") = []
{
    auto recorded = ClientWithRecordedLink {};
    recorded.client->writeUserInput(paneId, "ls\r");
    recorded.client->write(paneId, "\x1b[0n");

    check(recorded.hasSent(proto::make("write", {paneId, proto::userInputMarker}, "ls\r")));
    check(recorded.hasSent(proto::make("write", {paneId}, "\x1b[0n")));
};

auto tPhoneHoldingAPaneIsReported = test("DaemonClientSizeOwner/phoneHoldingAPaneIsReported") = []
{
    auto recorded = ClientWithRecordedLink {};
    auto notices = std::vector<DaemonClient::SizeOwnerNotice> {};
    auto routes = DaemonClient::Routes {};
    routes.onSizeOwnerChanged = [&](const DaemonClient::SizeOwnerNotice& notice)
    { notices.push_back(notice); };
    recorded.client->spawn({paneId, {phoneColumns, rows}, "/tmp", "Grazer", {}}, routes);

    recorded.link->deliver(makeSizeOwnerMessage(false, "iPhone"));

    check(recorded.client->hasPanesHeldElsewhere());
    check(notices.size() == 1 && !notices[0].isThisClientTheOwner);
    check(!notices.empty() && notices[0].ownerName == "iPhone");
    check(!notices.empty() && notices[0].size.cols == phoneColumns);

    recorded.link->deliver(makeSizeOwnerMessage(true, "Mac"));
    check(!recorded.client->hasPanesHeldElsewhere());
};

auto tPhoneClaimsWhatItOpensAndResizes = test("DaemonClientSizeOwner/phoneClaimsWhatItOpensAndResizes") = []
{
    constexpr auto narrowerColumns = 43;
    DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
    auto recorded = ClientWithRecordedLink {};
    recorded.client->spawn({paneId, {phoneColumns, rows}, "/tmp", "Grazer", {}}, {});
    recorded.client->resize(paneId, {narrowerColumns, rows});
    DaemonClient::setClaimsSizeOfEveryPaneItShows(false);

    check(recorded.hasSent(proto::make(
        "claim-size", {paneId, std::to_string(phoneColumns), std::to_string(rows)})));
    check(recorded.hasSent(proto::make(
        "claim-size", {paneId, std::to_string(narrowerColumns), std::to_string(rows)})));
    check(recorded.hasSent(proto::make(
        "resize", {paneId, std::to_string(narrowerColumns), std::to_string(rows)})),
          "a daemon from before size owners still resizes");
};

auto tSpawnCarriesTheSessionName = test("DaemonClientSizeOwner/spawnCarriesTheSessionName") = []
{
    auto recorded = ClientWithRecordedLink {};
    recorded.client->spawn({paneId, {phoneColumns, rows}, "/tmp", "my session", {}}, {});

    check(recorded.hasSent(proto::make(
        "spawn", {paneId, std::to_string(phoneColumns), std::to_string(rows), "my%20session"}, "/tmp")));
};

namespace
{
constexpr auto deskColumns = 150;
constexpr auto deskRows = 40;
constexpr auto protocolBeforeSizeOwners = proto::sizeOwnerProtocolVersion - 1;

std::string makeStateMessage(int columns, int rowCount)
{
    auto state = TerminalState {};
    state.screen.columns = columns;
    state.screen.rows = rowCount;
    return proto::make("state", {paneId}, serializeTerminalState(state));
}

std::string makeDaemonHello(int version)
{
    return proto::make("daemon-hello", {std::to_string(version)});
}

std::string makeResizeMessage(int columns, int rowCount)
{
    return proto::make("resize",
                       {paneId, std::to_string(columns), std::to_string(rowCount)});
}

std::string makeClaimMessage(int columns, int rowCount)
{
    return proto::make("claim-size",
                       {paneId, std::to_string(columns), std::to_string(rowCount)});
}

// The phone adopting a desk-sized pane on a daemon that answered hello with version.
struct PhoneHoldingDeskPane
{
    explicit PhoneHoldingDeskPane(int version)
    {
        DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
        recorded.link->deliver(makeDaemonHello(version));
        recorded.client->spawn({paneId, {phoneColumns, rows}, "/tmp", "Grazer", {}},
                               {});
        recorded.link->deliver(makeStateMessage(deskColumns, deskRows));
        recorded.sent.clear();
    }

    ~PhoneHoldingDeskPane() { DaemonClient::setClaimsSizeOfEveryPaneItShows(false); }

    PhoneHoldingDeskPane(const PhoneHoldingDeskPane&) = delete;
    PhoneHoldingDeskPane& operator=(const PhoneHoldingDeskPane&) = delete;

    ClientWithRecordedLink recorded;
};
} // namespace

auto tPhoneRemembersTheSizeItAdopted =
    test("DaemonClientSizeOwner/phoneRemembersTheSizeItAdopted") = []
{
    auto phone = PhoneHoldingDeskPane {protocolBeforeSizeOwners};
    phone.recorded.link->deliver(makeStateMessage(phoneColumns, rows));

    const auto borrowed = phone.recorded.client->findBorrowedSize(paneId);
    check(borrowed && borrowed->cols == deskColumns && borrowed->rows == deskRows,
          "the first state's size, not the one after a reconnect");
};

auto tPhoneGivesTheSizeBackOnRelease =
    test("DaemonClientSizeOwner/phoneGivesTheSizeBackOnRelease") = []
{
    auto phone = PhoneHoldingDeskPane {protocolBeforeSizeOwners};
    phone.recorded.client->release(paneId);

    const auto& sent = phone.recorded.sent;
    check(sent.size() == 2 && sent[0] == makeResizeMessage(deskColumns, deskRows));
    check(sent.size() == 2 && sent[1] == proto::make("release", {paneId}));
    check(!phone.recorded.client->findBorrowedSize(paneId));
};

auto tPhoneGivesSizesBackOnceAndClaimsThemAgain =
    test("DaemonClientSizeOwner/phoneGivesSizesBackOnceAndClaimsThemAgain") = []
{
    auto phone = PhoneHoldingDeskPane {protocolBeforeSizeOwners};
    const auto returned = phone.recorded.client->returnBorrowedSizes();
    const auto returnedAgain = phone.recorded.client->returnBorrowedSizes();

    check(returned.size() == 1 && returned[0].size.cols == deskColumns);
    check(returnedAgain.empty());
    check(phone.recorded.sent
          == std::vector {makeResizeMessage(deskColumns, deskRows)});

    phone.recorded.sent.clear();
    phone.recorded.client->claimReturnedSizesAgain();
    check(phone.recorded.sent
          == std::vector {makeResizeMessage(phoneColumns, rows),
                          makeClaimMessage(phoneColumns, rows)});
};

auto tDaemonWithSizeOwnersGetsNoSizeBack =
    test("DaemonClientSizeOwner/daemonWithSizeOwnersGetsNoSizeBack") = []
{
    auto phone = PhoneHoldingDeskPane {proto::sizeOwnerProtocolVersion};

    check(phone.recorded.client->returnBorrowedSizes().empty());
    phone.recorded.client->release(paneId);
    check(phone.recorded.sent == std::vector {proto::make("release", {paneId})});
};

auto tPhoneClaimsOnlyOnceTheListedPaneIsOpened =
    test("DaemonClientSizeOwner/phoneClaimsOnlyOnceTheListedPaneIsOpened") = []
{
    DaemonClient::setClaimsSizeOfEveryPaneItShows(true);
    DaemonClient::setOpensOnlyListedPanes(true);
    auto recorded = ClientWithRecordedLink {};
    recorded.client->spawn({paneId, {phoneColumns, rows}, "/tmp", "Grazer", {}}, {});
    recorded.client->resize(paneId, {phoneColumns, rows});
    const auto sentBeforeInfo = recorded.sent;
    recorded.link->deliver(
        proto::make("info", {}, std::string {paneId} + "\tzsh\t/tmp"));
    DaemonClient::setOpensOnlyListedPanes(false);
    DaemonClient::setClaimsSizeOfEveryPaneItShows(false);

    check(std::ranges::find(sentBeforeInfo, makeResizeMessage(phoneColumns, rows))
              == sentBeforeInfo.end(),
          "no resize reaches the pane before the daemon's first state");
    check(recorded.hasSent(makeClaimMessage(phoneColumns, rows)));
};
