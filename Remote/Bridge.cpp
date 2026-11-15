#include "Bridge.h"

#include "BridgeRelay.h"
#include "DaemonDialer.h"
#include "Protocol.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/Threads/Async.h>
#include <eacp/Core/Threads/Timer.h>

#include <array>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <unistd.h>

namespace term::bridge
{
using namespace eacp;

namespace
{
constexpr int exitOk = 0;
constexpr int exitFail = 1;
constexpr auto daemonHelloTimeout = Time::MS {1500};
constexpr auto readChunkBytes = std::size_t {65536};
constexpr int standardInput = 0;
constexpr int standardOutput = 1;

bool writeAllToStandardOutput(const std::string& bytes)
{
    auto written = std::size_t {0};

    while (written < bytes.size())
    {
        const auto result =
            ::write(standardOutput, bytes.data() + written, bytes.size() - written);

        if (result <= 0)
            return false;

        written += static_cast<std::size_t>(result);
    }

    return true;
}

std::optional<int> parseDaemonHello(const std::string& body)
{
    const auto message = proto::parse(body);

    if (message.verb != "daemon-hello" || message.args.empty())
        return std::nullopt;

    return std::stoi(message.args[0]);
}

struct BridgeApp
{
    explicit BridgeApp(std::string ctVersionToUse)
        : ctVersion(std::move(ctVersionToUse))
    {
        Apps::setDockIconVisible(false);
        std::signal(SIGPIPE, SIG_IGN);

        relay.writeToApp = [](const std::string& bytes)
        {
            if (!writeAllToStandardOutput(bytes))
                Apps::quit(exitFail);
        };

        if (const auto* home = std::getenv("HOME"))
            relay.homeDirectory = home;

        relay.sendToDaemon = [this](const std::string& message)
        {
            if (daemon != nullptr)
                daemon->send(message);
        };

        dial();
    }

    void dial()
    {
        daemon::dialOrLaunchDaemon(
            [this, guard = std::weak_ptr<bool> {alive}](std::unique_ptr<IPC::Messenger> messenger)
            {
                if (!guard.expired())
                    adoptDaemon(std::move(messenger));
            });
    }

    void adoptDaemon(std::unique_ptr<IPC::Messenger> messenger)
    {
        if (messenger == nullptr)
            return fail("cowtermd did not start (is it installed beside ct?)");

        daemon = std::move(messenger);
        daemon->onMessage = [this](std::string body) { handleDaemonMessage(std::move(body)); };
        daemon->onDisconnected = [this] { handleDaemonGone(); };
        daemon->send(proto::make("hello"));
        helloTimer = std::make_unique<Threads::Timer>([this] { restartOutdatedDaemon(); },
                                                      daemonHelloTimeout);
    }

    void handleDaemonMessage(std::string body)
    {
        if (ready)
            return relay.receiveFromDaemon(body);

        const auto version = parseDaemonHello(body);

        if (version == proto::daemonProtocolVersion)
            return startRelaying();

        if (version)
            restartOutdatedDaemon();
    }

    // An older cowtermd from before an update: it saves its panes and quits,
    // and the new one restores them, as after any daemon restart.
    void restartOutdatedDaemon()
    {
        helloTimer.reset();

        if (restartedDaemon)
            return startRelaying();

        restartedDaemon = true;
        restartingDaemon = true;
        daemon->send(proto::make("quit-server"));
    }

    void handleDaemonGone()
    {
        if (!restartingDaemon)
            return fail("cowtermd went away");

        restartingDaemon = false;
        Threads::callAsync([this, guard = std::weak_ptr<bool> {alive}]
                           {
                               if (!guard.expired())
                               {
                                   daemon.reset();
                                   dial();
                               }
                           });
    }

    void startRelaying()
    {
        helloTimer.reset();
        ready = true;
        relay.announceReady(ctVersion);
        startReadingStandardInput();
    }

    void startReadingStandardInput()
    {
        auto reader = std::thread(
            [guard = std::weak_ptr<bool> {alive}, this]
            {
                auto buffer = std::array<char, readChunkBytes> {};

                while (true)
                {
                    const auto count = ::read(standardInput, buffer.data(), buffer.size());

                    if (count <= 0)
                        break;

                    auto chunk = std::string(buffer.data(), static_cast<std::size_t>(count));
                    Threads::callAsync([this, guard, chunk = std::move(chunk)]
                                       {
                                           if (!guard.expired()
                                               && !relay.receiveFromApp(chunk))
                                               Apps::quit(exitFail);
                                       });
                }

                Threads::callAsync([guard]
                                   {
                                       if (!guard.expired())
                                           Apps::quit(exitOk);
                                   });
            });

        // A read blocked on stdin can't be interrupted; the process exit ends it.
        reader.detach();
    }

    void fail(const std::string& reason)
    {
        relay.announceFailure(reason);
        Apps::quit(exitFail);
    }

    std::string ctVersion;
    BridgeRelay relay;
    std::unique_ptr<IPC::Messenger> daemon;
    std::unique_ptr<Threads::Timer> helloTimer;
    bool ready = false;
    bool restartedDaemon = false;
    bool restartingDaemon = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace

int runBridge(const std::string& ctVersion)
{
    return Apps::run<BridgeApp>(ctVersion);
}
} // namespace term::bridge
