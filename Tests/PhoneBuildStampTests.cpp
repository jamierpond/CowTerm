#include <Multiplexer/AgeText.h>
#include <Multiplexer/PhoneBuildStamp.h>
#include <Multiplexer/Protocol.h>

#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <string>

using namespace nano;
using namespace term;

namespace
{
constexpr auto secondsPerMinute = std::int64_t {60};
constexpr auto secondsPerHour = 60 * secondsPerMinute;
constexpr auto secondsPerDay = 24 * secondsPerHour;
constexpr auto millisecondsPerSecond = std::int64_t {1000};
constexpr auto macCommitSeconds = std::int64_t {1763600000};
constexpr auto threeDays = 3 * secondsPerDay;

const auto macBuild = PhoneBuildStamp {macCommitSeconds, "abc1234"};

PhoneBuildStamp makeStampSecondsBeforeTheMac(std::int64_t seconds)
{
    return {macCommitSeconds - seconds, "0ld0001"};
}

std::string makePhoneLine(const PhoneBuildStamp& stamp)
{
    return formatConnectedClientLine({proto::phoneClientName, formatPhoneBuildStamp(stamp)});
}
} // namespace

auto tStampRoundTrips = test("PhoneBuildStamp/aStampReadsBackAsItWasWritten") = []
{
    const auto text = formatPhoneBuildStamp(macBuild);
    const auto parsed = parsePhoneBuildStamp(text);

    check(text == "1763600000-abc1234");
    check(parsed && parsed->commitSecondsSinceEpoch == macCommitSeconds && parsed->commitHash == "abc1234");
};

auto tNotAStamp = test("PhoneBuildStamp/textThatIsNotAStampReadsAsNone") = []
{
    check(!parsePhoneBuildStamp(""));
    check(!parsePhoneBuildStamp("0-"), "a build made without git");
    check(!parsePhoneBuildStamp("abc1234"));
    check(!parsePhoneBuildStamp("soon-abc1234"));
    check(!parsePhoneBuildStamp("1763600000-"));
};

auto tOlderPhone = test("PhoneBuildStamp/anOlderPhoneIsBehind") = []
{
    check(describeHowFarPhoneIsBehind(macBuild, makeStampSecondsBeforeTheMac(threeDays)) == "3 days behind");
};

auto tSamePhone = test("PhoneBuildStamp/aPhoneFromTheSameCommitIsNotBehind") = []
{
    check(!describeHowFarPhoneIsBehind(macBuild, macBuild));
};

auto tNewerPhone = test("PhoneBuildStamp/aNewerPhoneIsNotBehind") = []
{
    check(!describeHowFarPhoneIsBehind(macBuild, makeStampSecondsBeforeTheMac(-secondsPerHour)));
};

auto tPhoneWithoutStamp = test("PhoneBuildStamp/aPhoneThatSentNoStampIsFromBeforeStamps") = []
{
    check(describeHowFarPhoneIsBehind(macBuild, std::nullopt) == "from before build stamps");
};

auto tAgesInWords = test("PhoneBuildStamp/howFarBehindIsSaidInWholeUnits") = []
{
    const auto describe = [](std::int64_t seconds)
    { return describeAgeAsDuration(seconds * millisecondsPerSecond); };

    check(describe(secondsPerDay) == "1 day");
    check(describe(threeDays + secondsPerHour) == "3 days");
    check(describe(secondsPerHour) == "1 hour");
    check(describe(5 * secondsPerHour) == "5 hours");
    check(describe(secondsPerMinute) == "1 minute");
    check(describe(12 * secondsPerMinute) == "12 minutes");
    check(describe(secondsPerMinute - 1) == "less than a minute");
};

auto tClientLinesRoundTrip = test("PhoneBuildStamp/connectedClientLinesReadBack") = []
{
    const auto payload = formatConnectedClientLine({"my Mac", ""}) + makePhoneLine(macBuild);
    const auto clients = parseConnectedClients(payload);

    check(clients.size() == 2);
    check(clients[0].name == "my Mac" && !clients[0].stamp);
    check(clients[1].name == proto::phoneClientName && clients[1].stamp
          && clients[1].stamp->commitHash == macBuild.commitHash);
};

auto tNoticeOncePerStamp = test("PhoneBuildStamp/aPhoneIsNoticedOncePerStamp") = []
{
    auto notices = OutdatedPhoneNotices {macBuild};
    const auto behind = makePhoneLine(makeStampSecondsBeforeTheMac(threeDays));

    const auto first = notices.takeConnectedClients(behind);
    check(first.size() == 1);
    check(first[0].clientName == proto::phoneClientName && first[0].howFarBehind == "3 days behind");
    check(first[0].body.find("just ios-device pond") != std::string::npos);
    check(notices.takeConnectedClients(behind).empty(), "the next poll, or another daemon, says nothing new");

    const auto stillBehindAfterAnUpdate = makePhoneLine(makeStampSecondsBeforeTheMac(secondsPerHour));
    check(notices.takeConnectedClients(stillBehindAfterAnUpdate).size() == 1, "a new stamp is noticed");
};

auto tOnlyPhonesBehind = test("PhoneBuildStamp/onlyPhonesBehindTheMacAreNoticed") = []
{
    auto notices = OutdatedPhoneNotices {macBuild};
    const auto payload = formatConnectedClientLine({"Mac", ""}) + makePhoneLine(macBuild);

    check(notices.takeConnectedClients(payload).empty());
};

auto tPhoneWithoutStampNoticed = test("PhoneBuildStamp/aPhoneWithoutAStampIsNoticedOnce") = []
{
    auto notices = OutdatedPhoneNotices {macBuild};
    const auto payload = formatConnectedClientLine({proto::phoneClientName, ""});

    const auto first = notices.takeConnectedClients(payload);
    check(first.size() == 1 && first[0].howFarBehind == "from before build stamps");
    check(notices.takeConnectedClients(payload).empty());
};

auto tMacWithoutStamp = test("PhoneBuildStamp/aMacBuiltWithoutAStampNoticesNothing") = []
{
    auto notices = OutdatedPhoneNotices {std::nullopt};

    check(notices.takeConnectedClients(formatConnectedClientLine({proto::phoneClientName, ""})).empty());
};

auto tOldDaemonReadsTheHelloAsBefore = test("PhoneBuildStamp/aDaemonFromBeforeStampsReadsTheNewHelloAsBefore") = []
{
    const auto version = std::to_string(proto::daemonProtocolVersion);
    const auto before = proto::parse(proto::make("hello", {version, proto::phoneClientName}));
    const auto now =
        proto::parse(proto::make("hello", {version, proto::phoneClientName, formatPhoneBuildStamp(macBuild)}));

    check(now.args.size() == before.args.size() + 1);
    check(now.args[0] == before.args[0] && now.args[1] == before.args[1]);
};
