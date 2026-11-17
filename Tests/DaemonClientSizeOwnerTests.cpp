#include <Multiplexer/DaemonClient.h>
#include <Multiplexer/Protocol.h>

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
    check(!recorded.hasSent(proto::make(
        "resize", {paneId, std::to_string(narrowerColumns), std::to_string(rows)})));
};

auto tSpawnCarriesTheSessionName = test("DaemonClientSizeOwner/spawnCarriesTheSessionName") = []
{
    auto recorded = ClientWithRecordedLink {};
    recorded.client->spawn({paneId, {phoneColumns, rows}, "/tmp", "my session", {}}, {});

    check(recorded.hasSent(proto::make(
        "spawn", {paneId, std::to_string(phoneColumns), std::to_string(rows), "my%20session"}, "/tmp")));
};
