#include "PanePushNotifier.h"

#include "Multiplexer/SessionOverview.h"

#include <SessionLog/EventLine.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Network/HTTP/HttpAsync.h>

#include <chrono>

namespace
{
constexpr auto noticesPath = "/notices";
constexpr auto relayAnswerTimeout = std::chrono::milliseconds {std::chrono::minutes {1}};

void logPushEvent(const std::string& paneId, const std::string& state, std::string_view outcome)
{
    eacp::LOG(sessionlog::formatEventLine(
        "daemon.push", {{"pane", paneId}, {"state", state}, {"outcome", std::string {outcome}}}));
}
} // namespace

PanePushNotifier::PanePushNotifier(PushRelayTarget targetToUse)
    : target(std::move(targetToUse))
{
}

void PanePushNotifier::reportPaneState(const PaneForPush& pane,
                                       const term::push::PaneStateChange& change,
                                       const term::push::PaneAttention& attention)
{
    if (target.relayAddress.empty() || target.hostName.empty())
        return;

    const auto verdict = gates[pane.paneId].reportStateChange(change, attention);

    if (verdict != term::push::PushVerdict::notFinishedOrWaiting)
        logPushEvent(pane.paneId, change.state, term::push::describePushVerdict(verdict));

    if (verdict != term::push::PushVerdict::push)
        return;

    postToRelay({target.hostName,
                 term::overview::nameSessionOfPane(pane.sessionName, pane.workingDirectory),
                 pane.paneId,
                 change.state,
                 change.message});
}

void PanePushNotifier::forgetPane(const std::string& paneId)
{
    gates.erase(paneId);
}

void PanePushNotifier::postToRelay(const term::push::PushNotice& notice) const
{
    auto request = eacp::HTTP::Request::post(target.relayAddress + noticesPath,
                                             term::push::writePushNoticeJson(notice));
    request.headers["content-type"] = "application/json";
    request.timeout = eacp::Time::MS {relayAnswerTimeout.count()};

    eacp::HTTP::asyncRequest(
        request,
        [paneId = notice.paneId, state = notice.event](const eacp::HTTP::Response& response)
        {
            auto answer = std::to_string(response.statusCode) + " " + response.content
                          + response.error;

            while (answer.ends_with('\n'))
                answer.pop_back();

            logPushEvent(paneId, state, "relay answered " + answer);
        });
}
