#include "PhoneBuildStamp.h"

#include "AgeText.h"
#include "Protocol.h"

#include <algorithm>
#include <charconv>

namespace term
{
namespace
{
constexpr auto stampSeparator = '-';
constexpr auto columnSeparator = '\t';
constexpr auto millisecondsPerSecond = std::int64_t {1000};
constexpr auto fixCommand = "Run just ios-device pond in ~/projects/CowTerm to update it.";

std::string makePhoneKey(const ConnectedClientReport& phone)
{
    if (!phone.stamp)
        return phone.name;

    return phone.name + columnSeparator + formatPhoneBuildStamp(*phone.stamp);
}

std::string describeWhatItIsBehind(const ConnectedClientReport& phone)
{
    if (!phone.stamp)
        return {};

    return " this Mac's";
}

void writeNoticeText(OutdatedPhoneNotice& notice, const ConnectedClientReport& phone)
{
    notice.title = "CowTerm on " + phone.name + " is out of date";
    notice.body = "Its build is " + notice.howFarBehind + describeWhatItIsBehind(phone) + ". " + fixCommand;
}
} // namespace

std::string formatPhoneBuildStamp(const PhoneBuildStamp& stamp)
{
    return std::to_string(stamp.commitSecondsSinceEpoch) + stampSeparator + stamp.commitHash;
}

std::optional<PhoneBuildStamp> parsePhoneBuildStamp(std::string_view text)
{
    const auto separator = text.find(stampSeparator);

    if (separator == std::string_view::npos || separator + 1 == text.size())
        return std::nullopt;

    auto seconds = std::int64_t {0};
    const auto secondsText = text.substr(0, separator);
    const auto* secondsEnd = secondsText.data() + secondsText.size();
    const auto [stoppedAt, error] = std::from_chars(secondsText.data(), secondsEnd, seconds);
    const auto isWholeNumber = error == std::errc {} && stoppedAt == secondsEnd;

    if (!isWholeNumber || seconds <= 0)
        return std::nullopt;

    return PhoneBuildStamp {seconds, std::string {text.substr(separator + 1)}};
}

std::optional<std::string> describeHowFarPhoneIsBehind(const PhoneBuildStamp& thisBuild,
                                                       const std::optional<PhoneBuildStamp>& phone)
{
    if (!phone)
        return "from before build stamps";

    const auto secondsBehind = thisBuild.commitSecondsSinceEpoch - phone->commitSecondsSinceEpoch;

    if (secondsBehind <= 0)
        return std::nullopt;

    return describeAgeAsDuration(secondsBehind * millisecondsPerSecond) + " behind";
}

std::string formatConnectedClientLine(const ConnectedClientLine& client)
{
    auto line = proto::encodeArgument(client.name) + columnSeparator + client.stampText;
    std::ranges::replace(line, '\n', ' ');
    return line + '\n';
}

std::vector<ConnectedClientReport> parseConnectedClients(const std::string& payload)
{
    auto clients = std::vector<ConnectedClientReport> {};
    auto start = std::size_t {0};

    while (start < payload.size())
    {
        const auto end = std::min(payload.find('\n', start), payload.size());
        const auto line = std::string_view {payload}.substr(start, end - start);
        const auto tab = line.find(columnSeparator);

        if (tab != std::string_view::npos)
            clients.push_back({proto::decodeArgument(line.substr(0, tab)), parsePhoneBuildStamp(line.substr(tab + 1))});

        start = end + 1;
    }

    return clients;
}

OutdatedPhoneNotices::OutdatedPhoneNotices(std::optional<PhoneBuildStamp> thisBuildToUse)
    : thisBuild(std::move(thisBuildToUse))
{
}

std::vector<OutdatedPhoneNotice> OutdatedPhoneNotices::takeConnectedClients(const std::string& payload)
{
    auto notices = std::vector<OutdatedPhoneNotice> {};

    if (!thisBuild)
        return notices;

    for (const auto& client: parseConnectedClients(payload))
    {
        if (client.name != proto::phoneClientName)
            continue;

        if (auto notice = takePhone(client))
            notices.push_back(std::move(*notice));
    }

    return notices;
}

std::optional<OutdatedPhoneNotice> OutdatedPhoneNotices::takePhone(const ConnectedClientReport& phone)
{
    const auto howFarBehind = describeHowFarPhoneIsBehind(*thisBuild, phone.stamp);
    const auto key = makePhoneKey(phone);

    if (!howFarBehind || std::ranges::find(phonesAlreadyNoticed, key) != phonesAlreadyNoticed.end())
        return std::nullopt;

    phonesAlreadyNoticed.push_back(key);
    auto notice = OutdatedPhoneNotice {};
    notice.clientName = phone.name;
    notice.howFarBehind = *howFarBehind;
    writeNoticeText(notice, phone);
    return notice;
}
} // namespace term
