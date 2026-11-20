#include "OutdatedPhoneNag.h"

#include "Notifier.h"
#include "Multiplexer/Protocol.h"
#include "Multiplexer/UsageLog.h"

namespace term
{
namespace
{
// A phone that just connected is noticed within this long.
constexpr auto askInterval = eacp::Time::MS {30000};
constexpr auto thisMacsDaemon = "local";

std::string describeHost(const std::string& host)
{
    if (host.empty())
        return thisMacsDaemon;

    return host;
}
} // namespace

OutdatedPhoneNag::OutdatedPhoneNag(std::optional<PhoneBuildStamp> thisBuild,
                                   std::function<void(const std::string& message)> sendToEveryDaemonToUse)
    : sendToEveryDaemon(std::move(sendToEveryDaemonToUse))
    , notices(std::move(thisBuild))
    , askTimer([this] { sendToEveryDaemon(proto::make(clientsVerb)); }, askInterval)
{
}

void OutdatedPhoneNag::takeConnectedClients(const std::string& host, const std::string& body)
{
    for (const auto& notice: notices.takeConnectedClients(proto::parse(body).payload))
    {
        logUsage("phone.out-of-date",
                 {{"client", notice.clientName}, {"behind", notice.howFarBehind}, {"host", describeHost(host)}});
        Notifier::notify({}, notice.title, notice.body);
    }
}
} // namespace term
