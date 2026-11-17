#include "PhonePairing.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace cowterm::mobile;

namespace
{
DeviceKey makeDeviceKey()
{
    auto key = DeviceKey {};
    key.signingKey.publicKeyBlob = std::string {"\x00\x00\x00\x13" "ecdsa-sha2-nistp256", 23};
    key.signingKey.sign = [](std::string_view) { return std::optional<std::string> {"signature"}; };
    return key;
}
} // namespace

auto deviceKeySignsWhenNoKeyFileIsPaired = test("PhonePairing/deviceKeySignsWhenNoKeyFileIsPaired") = []
{
    const auto settings = useDeviceKeyWithoutKeyFile(term::phone::PhoneSettings {}, makeDeviceKey());

    check(settings.has_value());
    check(settings->credentials.signingKey.has_value());
};

auto keyFileFromIosPairWins = test("PhonePairing/keyFileFromIosPairWins") = []
{
    auto paired = term::phone::PhoneSettings {};
    paired.credentials.privateKeyPath = "pairing/phone-key";
    const auto settings = useDeviceKeyWithoutKeyFile(paired, makeDeviceKey());

    check(!settings->credentials.signingKey.has_value());
};

auto noSettingsStayNone = test("PhonePairing/noSettingsStayNone") = []
{
    check(!useDeviceKeyWithoutKeyFile(std::nullopt, makeDeviceKey()).has_value());
};

auto keyLineNamesTheIphone = test("PhonePairing/keyLineNamesTheIphone") = []
{
    const auto line = describeDeviceKeyLine(makeDeviceKey());

    check(line.starts_with("ecdsa-sha2-nistp256 "));
    check(line.ends_with(" cowterm-iphone"));
    check(describeDeviceKeyLine(std::nullopt).find("could not") != std::string::npos);
};

auto launchFlagsNameTheHostAndTrust = test("PhonePairing/launchFlagsNameTheHostAndTrust") = []
{
    const auto flags = parsePairingLaunchFlags({"CowTermMobile", "--pair-with=jamie@studio:2222", "--pair-trust"});

    check(flags.hostToPairWith == "jamie@studio:2222");
    check(flags.trustsOfferedHostKey);
    check(!parsePairingLaunchFlags({"CowTermMobile"}).trustsOfferedHostKey);
};
