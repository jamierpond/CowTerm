#include "DaemonConversation.h"

#include <Multiplexer/Protocol.h>
#include <PondApp/DataDirectory.h>
#include <Multiplexer/SessionOverview.h>
#include <Push/PushNotice.h>

#include <NanoTest/NanoTest.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Network/HTTPServer/HttpServer.h>

#include <algorithm>
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
    {
        process.emplace(launchDaemon(instance));
    }

    // Quits the daemon the way an update does and starts it again; panes come
    // back when a client spawns them again.
    void restart()
    {
        auto last = DaemonConversation {instance};

        if (last.waitForConnection())
            last.quitDaemon();

        process->wait();
        process.emplace(launchDaemon(instance));
    }

    ~PushingDaemon()
    {
        auto last = DaemonConversation {instance};

        if (last.waitForConnection())
            last.quitDaemon();

        process->wait();
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
    std::optional<Processes::Process> process;
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

void viewPane(DaemonConversation& client, const std::string& id)
{
    client.messenger.send(term::proto::make("viewing", {id}));
}

// The daemon's activity line for the pane, as a client reads it. The daemon
// answers each client in order, but not two clients in each other's order, so a
// test reads on the client that sent last before another client reads.
term::overview::PaneActivityReport readActivity(DaemonConversation& client, const std::string& id)
{
    client.messenger.send(term::proto::make("activity"));
    const auto answer = client.waitForMessage("activity");

    if (!answer)
        return {};

    for (const auto& report: term::overview::parseActivityReport(answer->payload))
    {
        if (report.paneId == id)
            return report;
    }

    return {};
}

// Every notice the daemon will post for what came before has been sent once a
// later one is: the daemon handles messages in order. The posts themselves run
// in parallel, so they may reach the relay in any order.
std::vector<term::push::PushNotice> pushOnceMoreAndCollect(DaemonConversation& client,
                                                            FakeRelay& relay,
                                                            std::size_t expectedCount)
{
    createPane(client, quietPaneId);
    reportState(client, quietPaneId, "finished", "marker");
    return relay.waitForNotices(expectedCount);
}
} // namespace

auto tFinishedPaneIsPostedToTheRelay = test("DaemonUnread/finishedPaneIsPostedToTheRelay") = []
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

auto tTheSameEventIsPostedOnce = test("DaemonUnread/theSameEventIsPostedOnce") = []
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
    const auto postsForThePane = std::ranges::count(notices, std::string {paneId}, &term::push::PushNotice::paneId);
    check(notices.size() == 2, std::to_string(notices.size()));
    check(postsForThePane == 1, "the posts run in parallel, so they may arrive in either order");
};

auto tAFinishedPaneIsUnreadForEveryClient = test("DaemonUnread/aFinishedPaneIsUnreadForEveryClient") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-finished-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, term::proto::phoneClientName);
    createPane(mac, paneId);

    reportState(mac, paneId, "finished", "All 420 tests pass.");
    readActivity(mac, paneId);

    const auto seenByPhone = readActivity(phone, paneId);
    check(seenByPhone.unreadEvent == "finished", seenByPhone.unreadEvent);
    check(seenByPhone.unreadText == "All 420 tests pass.");
    check(seenByPhone.unreadSinceMilliseconds > 0);
    check(relay.waitForNotices(1).size() == 1, "an unread event is what a push announces");
};

auto tSeenOnTheMacIsReadOnThePhone = test("DaemonUnread/seenOnTheMacIsReadOnThePhone") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-seen-mac-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, term::proto::phoneClientName);
    createPane(mac, paneId);
    reportState(mac, paneId, "finished", "Done.");

    viewPane(mac, paneId);
    readActivity(mac, paneId);

    check(readActivity(phone, paneId).unreadEvent.empty());
};

auto tSeenOnThePhoneIsReadOnTheMac = test("DaemonUnread/seenOnThePhoneIsReadOnTheMac") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-seen-phone-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    auto phone = DaemonConversation {daemon.instance};
    check(mac.waitForConnection() && phone.waitForConnection());
    introduce(mac, "Mac");
    introduce(phone, term::proto::phoneClientName);
    createPane(mac, paneId);
    reportState(mac, paneId, "waiting", "Allow Bash?");

    viewPane(phone, paneId);
    readActivity(phone, paneId);

    check(readActivity(mac, paneId).unreadEvent.empty());
};

auto tFinishingWhileViewedIsNeitherUnreadNorPushed = test("DaemonUnread/finishingWhileViewedIsNeitherUnreadNorPushed") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-viewed-test", relay};
    auto phone = DaemonConversation {daemon.instance};
    check(phone.waitForConnection());
    introduce(phone, term::proto::phoneClientName);
    createPane(phone, paneId);
    viewPane(phone, paneId);

    reportState(phone, paneId, "finished", "Done.");

    check(readActivity(phone, paneId).unreadEvent.empty());
    const auto notices = pushOnceMoreAndCollect(phone, relay, 1);
    check(notices.size() == 1 && notices[0].paneId == quietPaneId,
          "only the marker pane, which nobody was viewing");
};

auto tAPromptSubmittedIsRead = test("DaemonUnread/aPromptSubmittedIsRead") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-prompt-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);
    reportState(mac, paneId, "waiting", "Allow Bash?");

    reportState(mac, paneId, "working", "");

    check(readActivity(mac, paneId).unreadEvent.empty());
};

auto tAViewerThatDisconnectsStopsViewing = test("DaemonUnread/aViewerThatDisconnectsStopsViewing") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-disconnect-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);

    {
        auto phone = DaemonConversation {daemon.instance};
        check(phone.waitForConnection());
        introduce(phone, term::proto::phoneClientName);
        viewPane(phone, paneId);
        readActivity(phone, paneId);
    }

    // The daemon hears of the disconnect on its own time, so report until it has.
    constexpr auto mostReports = 50;
    auto activity = term::overview::PaneActivityReport {};

    for (auto report = 0; report < mostReports && activity.unreadEvent.empty(); ++report)
    {
        reportState(mac, paneId, "finished", "Done " + std::to_string(report));
        activity = readActivity(mac, paneId);
    }

    check(activity.unreadEvent == "finished");
};

auto tUnreadSurvivesADaemonRestart = test("DaemonUnread/unreadSurvivesADaemonRestart") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-restart-test", relay};

    {
        auto mac = DaemonConversation {daemon.instance};
        check(mac.waitForConnection());
        introduce(mac, "Mac");
        createPane(mac, paneId);
        reportState(mac, paneId, "finished", "Kept across the restart.");
        readActivity(mac, paneId);
    }

    daemon.restart();
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);

    const auto activity = readActivity(mac, paneId);
    check(activity.unreadEvent == "finished", activity.unreadEvent);
    check(activity.unreadText == "Kept across the restart.");
};

auto tAShellBellIsUnread = test("DaemonUnread/aShellBellIsUnread") = []
{
    auto relay = FakeRelay {};
    auto daemon = PushingDaemon {"daemon-unread-bell-test", relay};
    auto mac = DaemonConversation {daemon.instance};
    check(mac.waitForConnection());
    introduce(mac, "Mac");
    createPane(mac, paneId);

    mac.messenger.send(term::proto::make("write", {paneId}, "printf '\\a'\r"));

    const auto notices = relay.waitForNotices(1);
    check(notices.size() == 1 && notices[0].event == "bell");
    check(readActivity(mac, paneId).unreadEvent == "bell");
};
