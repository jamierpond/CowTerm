#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// The last commit that changed what the iPhone app is built from
// (cmake/PhoneBuildStamp.cmake lists the paths): when, and its short hash.
struct PhoneBuildStamp
{
    std::int64_t commitSecondsSinceEpoch = 0;
    std::string commitHash;
};

// "1763600000-abc1234", the phone's last argument to hello.
std::string formatPhoneBuildStamp(const PhoneBuildStamp& stamp);

// Empty for text that is not a stamp, including the "0-" of a build made
// without git.
std::optional<PhoneBuildStamp> parsePhoneBuildStamp(std::string_view text);

// How far a connected phone is behind the stamp this Mac app was built with:
// "3 days behind", or "from before build stamps" for a phone that sent none.
// Empty when the phone is as new or newer.
std::optional<std::string> describeHowFarPhoneIsBehind(const PhoneBuildStamp& thisBuild,
                                                       const std::optional<PhoneBuildStamp>& phone);

// One client a daemon has, as its "clients" answer lists it.
struct ConnectedClientReport
{
    std::string name;
    std::optional<PhoneBuildStamp> stamp;
};

// What a daemon knows of one client: its name and the stamp text it sent.
struct ConnectedClientLine
{
    std::string name;
    std::string stampText;
};

// One "<name, percent-encoded>\t<stamp as the client sent it>\n" line.
std::string formatConnectedClientLine(const ConnectedClientLine& client);

std::vector<ConnectedClientReport> parseConnectedClients(const std::string& payload);

// A desktop notification for a phone that is behind this Mac app.
struct OutdatedPhoneNotice
{
    std::string clientName;
    std::string howFarBehind;
    std::string title;
    std::string body;
};

// Turns daemons' "clients" answers into notices: once for each phone and
// stamp while the app runs, however many daemons list it or how often. A Mac
// app built without a stamp gives none.
class OutdatedPhoneNotices
{
public:
    explicit OutdatedPhoneNotices(std::optional<PhoneBuildStamp> thisBuildToUse);

    std::vector<OutdatedPhoneNotice> takeConnectedClients(const std::string& payload);

private:
    std::optional<OutdatedPhoneNotice> takePhone(const ConnectedClientReport& phone);

    std::optional<PhoneBuildStamp> thisBuild;
    std::vector<std::string> phonesAlreadyNoticed;
};
} // namespace term
