#include <Phone/PhoneSettings.h>
#include <Phone/PushTarget.h>

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::phone;

namespace
{
term::overview::OverviewRow makeRow(const std::string& host,
                                    const std::string& session,
                                    std::vector<std::string> paneIds)
{
    auto row = term::overview::OverviewRow {};
    row.host = host;
    row.sessionName = session;
    row.paneIds = std::move(paneIds);
    return row;
}

struct DecodedRegistration
{
    std::string name;
    std::string token;
    std::string environment;

    MIRO_REFLECT(name, token, environment)
};

const auto rows = std::vector {makeRow("jamiepond@tamby", "Grazer", {"t1"}),
                               makeRow("jamiepond@macprorack", "Grazer", {"m1", "m2"}),
                               makeRow("jamiepond@macprorack", "CowTerm", {"m3"})};
} // namespace

auto tPayloadKeysNameThePane = test("PushTarget/payloadKeysNameThePane") = []
{
    const auto target =
        readNotificationTarget({{"host", "macprorack"}, {"session", "Grazer"}, {"pane", "m2"}});
    check(target && target->host == "macprorack" && target->session == "Grazer" && target->paneId == "m2");
    check(!readNotificationTarget({{"session", "Grazer"}}));
};

auto tHostLabelLosesUserAndPort = test("PushTarget/hostLabelLosesUserAndPort") = []
{
    check(getHostNameOfLabel("jamiepond@macprorack") == "macprorack");
    check(getHostNameOfLabel("jamie@tamby:2222") == "tamby");
    check(getHostNameOfLabel("big-cow") == "big-cow");
};

auto tTapFindsTheRowHoldingThePane = test("PushTarget/tapFindsTheRowHoldingThePane") = []
{
    const auto row = findRowForNotification(rows, {"macprorack", "Renamed", "m2"});
    check(row && row->host == "jamiepond@macprorack" && row->sessionName == "Grazer");
};

auto tTapFallsBackToTheSessionOnThatHost = test("PushTarget/tapFallsBackToTheSessionOnThatHost") = []
{
    const auto row = findRowForNotification(rows, {"macprorack", "CowTerm", "gone"});
    check(row && row->sessionName == "CowTerm");
    check(!findRowForNotification(rows, {"big-cow", "Grazer", "t1"}), "the pane is on tamby, not big-cow");
};

auto tRegistrationCarriesNameTokenAndEnvironment = test("PushTarget/registrationCarriesNameTokenAndEnvironment") = []
{
    const auto registration = Miro::createFromJSONString<DecodedRegistration>(
        makeDeviceRegistrationJson("pond", "abc123", "sandbox"));
    check(registration.name == "pond" && registration.token == "abc123");
    check(registration.environment == "sandbox");
};

auto tPairedSetupNamesTheRelay = test("PushTarget/pairedSetupNamesTheRelay") = []
{
    const auto settings = parsePhoneSettings(
        {"--destination", "jamiepond@macprorack", "--push-relay", "http://macprorack:8742\n", "--device-name", "pond"},
        [](const std::string&) { return std::string {}; },
        "/tmp/last-screen.json");
    check(settings && settings->pushRelayAddress == "http://macprorack:8742");
    check(settings && settings->deviceName == "pond");
};
