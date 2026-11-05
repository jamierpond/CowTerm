#include "NotifyUserClient.h"

#include "Protocol.h"

#include <eacp/Core/App/App.h>
#include <eacp/Network/IPC/Messenger.h>

#include <memory>

namespace term
{
using namespace eacp;

namespace
{
// The app is either up and serving or not; a long dial only delays the error.
constexpr auto dialTimeout = Time::MS {1500};

struct NotifyUserDialer
{
    NotifyUserDialer(NotifyUserRequest requestToSend,
                     std::function<int(const NotifyUserOutcome&)> exitCodeForOutcomeToUse)
        : request(std::move(requestToSend))
        , exitCodeForOutcome(std::move(exitCodeForOutcomeToUse))
    {
        // ct shares the app's bundle, so without this it would get a Dock tile.
        Apps::setDockIconVisible(false);

        messenger->onConnected = [this]
        { messenger->send(encodeNotifyUserRequest(request)); };

        messenger->onMessage = [this](const std::string& body)
        {
            if (const auto reply = decodeNotifyUserReply(body))
                finish({true, *reply});
        };

        messenger->onDisconnected = [this] { finish({}); };
    }

    void finish(const NotifyUserOutcome& outcome)
    {
        if (finished)
            return;

        finished = true;
        Apps::quit(exitCodeForOutcome(outcome));
    }

    NotifyUserRequest request;
    std::function<int(const NotifyUserOutcome&)> exitCodeForOutcome;
    bool finished = false;
    std::unique_ptr<IPC::Messenger> messenger =
        std::make_unique<IPC::Messenger>(proto::appServerName(), dialTimeout);
};
} // namespace

int askRunningAppAndWait(const NotifyUserRequest& request,
                         std::function<int(const NotifyUserOutcome&)> exitCodeForOutcome)
{
    return Apps::run<NotifyUserDialer>(request, std::move(exitCodeForOutcome));
}
} // namespace term
