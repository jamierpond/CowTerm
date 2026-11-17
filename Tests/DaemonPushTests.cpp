#include "DaemonConversation.h"

#include <Multiplexer/Protocol.h>
#include <PondApp/DataDirectory.h>
#include <Push/PushNotice.h>

#include <NanoTest/NanoTest.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Network/HTTPServer/HttpServer.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using namespace nano;
using namespace eacp;
using namespace daemontest;

namespace
{
constexpr auto paneId = "push-pane";
constexpr auto quietPaneId = "push-quiet-pane";
constexpr auto sessionName = "Grazer";
constexpr auto hostName = "test-host";
constexpr auto columns = 80;
constexpr auto phoneColumns = 49;
constexpr auto rows = 24;
constexpr auto okStatus = 200;
constexpr auto relayWait = Time::MS {15000};

// Stands in for `ct push-relay`: records every notice a daemon posts.
struct FakeRelay
{
    FakeRelay()
    {
        server.post("/notices",
                    [this](const HTTP::Request& request)
                    {
                        auto guard = std::scoped_lock {lock};
                        notices.push_back(request.body);
                        auto response = HTTP::Response {};
                        response.statusCode = okStatus;
                        response.setContent("delivered to 1 of 1 phones\n", "text/plain");
                        return response;
                    });
        isListening = server.listen(0);
    }

    std::string getAddress() const
    {
        return "http://127.0.0.1:" + std::to_string(server.boundPort());
    }

    std::vector<term::push::PushNotice> waitForNotices(std::size_t count)
    {
        Threads::runEventLoopUntil([&] { return countNotices() >= count; }, relayWait);
        auto guard = std::scoped_lock {lock};
        auto read = std::vector<term::push::PushNotice> {};

        for (const auto& body: notices)
        {
            if (auto notice = term::push::readPushNoticeJson(body))
                read.push_back(*notice);
        }

        return read;
    }

    std::size_t countNotices()
    {
        auto guard = std::scoped_lock {lock};
        return notices.size();
    }

    HTTP::Server server {HTTP::ServerOptions {HTTP::ServerThreadingMode::ThreadPool}};
    std::mutex lock;
    std::vector<std::string> notices;
    bool isListening = false;
};

std::filesystem::path getInstanceConfigPath(const std::string& instance)
{
    return std::filesystem::path {FilePath::homeDirectory().str()} / ".config"
           / ("cowterm." + instance + ".json");
}

// A daemon whose config sends its notices to the fake relay.
struct PushingDaemon
{
    PushingDaemon(const std::string& testName, const FakeRelay& relay)
        : instance(makeInstanceName(testName))
        , configPath(writeConfig(instance, relay))
        , process(launchDaemon(instance))
    {
    }

    ~PushingDaemon()
    {
        auto last = DaemonConversation {instance};

        if (last.waitForConnection())
            last.quitDaemon();

        process.wait();
        std::filesystem::remove(configPath);
        std::filesystem::remove_all(pondapp::getPondAppDataDirectory("CowTerm")
                                    / ("panes-" + instance));
    }

    static std::filesystem::path writeConfig(const std::string& instance, const FakeRelay& relay)
    {
        const auto path = getInstanceConfigPath(instance);
        auto file = std::ofstream {path};
        file << R"({"pushRelay":")" << relay.getAddress() << R"(","pushHostName":")" << hostName
             << R"("})";
        return path;
    }

    std::string instance;
    std::filesystem::path configPath;
    Processes::Process process;
};

void introduce(DaemonConversation& client, const std::string& name)
{
    client.messenger.send(term::proto::make(
        "hello",
        {std::to_string(term::proto::daemonProtocolVersion), term::proto::encodeArgument(name)}));
    client.waitForMessage("daemon-hello");
}

void createPane(DaemonConversation& client, const std::string& id)
{
    client.messenger.send(term::proto::make(
        "spawn", {id, std::to_string(columns), std::to_string(rows), sessionName}, "/tmp"));
    client.waitForMessage("output");
}

void reportState(DaemonConversation& client,
                 const std::string& id,
                 const std::string& state,
                 const std::string& message)
{
    client.messenger.send(term::proto::make("pane-state", {id, state}, message));
}

// Every notice the daemon will post for what came before has been posted once
// a later, unsuppressed one arrives: the daemon handles messages in order.
std::vector<term::push::PushNotice> pushOnceMoreAndCollect(DaemonConversation& client,
                                                            FakeRelay& relay,
                                                            std::size_t expectedCount)
{
    createPane(client, quietPaneId);
    reportState(client, quietPaneId, "finished", "marker");
    return relay.waitForNotices(expectedCount);
}
} // namespace

auto tFinishedPaneIsPostedToTheRelay = test("DaemonPush/finishedPaneIsPostedToTheRelay") = []
{
    auto relay = FakeRelay {};
    check(relay.isListening);
    auto daemon = PushingDaemon {"daemon-push-finished-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);

    reportState(mac, paneId, "working", "");
    reportState(mac, paneId, "finished", "All 420 tests pass.");

    const auto notices = relay.waitForNotices(1);
    check(notices.size() == 1, std::to_string(notices.size()));

    if (!notices.empty())
    {
        check(notices[0].host == hostName && notices[0].session == sessionName);
        check(notices[0].paneId == paneId && notices[0].event == "finished");
        check(notices[0].text == "All 420 tests pass.");
    }

    mac.messenger.send(term::proto::make("kill", {paneId}));
};

auto tTheSameEventIsPostedOnce = test("DaemonPush/theSameEventIsPostedOnce") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-push-once-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);

    reportState(mac, paneId, "waiting", "Claude needs your permission to use Bash");
    reportState(mac, paneId, "waiting", "Claude needs your permission to use Bash");

    const auto notices = pushOnceMoreAndCollect(mac, relay, 2);
    check(notices.size() == 2, std::to_string(notices.size()));
    check(notices.size() == 2 && notices[1].paneId == quietPaneId);
};

auto tPaneShownOnThePhoneIsNotPosted = test("DaemonPush/paneShownOnThePhoneIsNotPosted") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-push-phone-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, term::proto::phoneClientName);
    createPane(mac, paneId);
    phone.messenger.send(term::proto::make(
        "claim-size", {paneId, std::to_string(phoneColumns), std::to_string(rows)}));
    phone.waitForMessage("size-owner");

    reportState(mac, paneId, "finished", "Done.");

    const auto notices = pushOnceMoreAndCollect(mac, relay, 1);
    check(notices.size() == 1 && notices[0].paneId == quietPaneId,
          "only the marker pane, which the phone does not show");
};

auto tPaneTypedIntoOnTheDesktopIsNotPosted = test("DaemonPush/paneTypedIntoOnTheDesktopIsNotPosted") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-push-desktop-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);
    mac.messenger.send(term::proto::make("write", {paneId, term::proto::userInputMarker}, "\r"));

    reportState(mac, paneId, "finished", "Done.");

    const auto notices = pushOnceMoreAndCollect(mac, relay, 1);
    check(notices.size() == 1 && notices[0].paneId == quietPaneId,
          "only the marker pane, which nobody typed into");
};

auto tPhoneTypingDoesNotSuppress = test("DaemonPush/phoneTypingDoesNotSuppress") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-push-phone-typing-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, term::proto::phoneClientName);
    createPane(mac, paneId);
    phone.messenger.send(term::proto::make("write", {paneId, term::proto::userInputMarker}, "\r"));

    reportState(mac, paneId, "finished", "Done.");

    const auto notices = relay.waitForNotices(1);
    check(notices.size() == 1 && notices[0].paneId == paneId);
};
