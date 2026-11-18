#include "PanePushNotifier.h"

#include "Multiplexer/SessionOverview.h"

#include <Push/PushNotice.h>
#include <SessionLog/EventLine.h>
#include <eacp/Core/Utils/Logging.h>
#include <eacp/Network/HTTP/HttpAsync.h>

#include <chrono>

namespace
{
constexpr auto noticesPath = "/notices";
constexpr auto relayAnswerTimeout = std::chrono::milliseconds {std::chrono::minutes {1}};

void logPushEvent(const std::string& paneId, const std::string& event, const std::string& outcome)
{
    eacp::LOG(sessionlog::formatEventLine("daemon.push",
                                          {{"pane", paneId}, {"event", event}, {"outcome", outcome}}));
}
} // namespace

PanePushNotifier::PanePushNotifier(PushRelayTarget targetToUse)
    : target(std::move(targetToUse))
{
}

void PanePushNotifier::postNotice(const PaneForPush& pane, const term::UnreadEvent& event) const
{
    if (target.relayAddress.empty() || target.hostName.empty())
        return;

    const auto notice = term::push::PushNotice {target.hostName,
                                                term::overview::nameSessionOfPane(pane.sessionName,
                                                                                  pane.workingDirectory),
                                                pane.paneId,
                                                event.event,
                                                event.text};
    auto request =
        eacp::HTTP::Request::post(target.relayAddress + noticesPath, term::push::writePushNoticeJson(notice));
    request.headers["content-type"] = "application/json";
    request.timeout = eacp::Time::MS {relayAnswerTimeout.count()};

    eacp::HTTP::asyncRequest(
        request,
        [paneId = notice.paneId, eventName = notice.event](const eacp::HTTP::Response& response)
        {
            auto answer = std::to_string(response.statusCode) + " " + response.content + response.error;

            while (answer.ends_with('\n'))
                answer.pop_back();

            logPushEvent(paneId, eventName, "relay answered " + answer);
        });
}
