#include "Multiplexer/HostColours.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto hostColourDependsOnlyOnTheName = test("HostColours/hostColourDependsOnlyOnTheName") = []
{
    const auto config = AppConfig {};

    const auto tambyByAlias = getColourBlindSafeHostRowColours(config, "tamby");
    const auto tambyWithUser = getColourBlindSafeHostRowColours(config, "jamiepond@tamby");

    check(tambyWithUser.background == tambyByAlias.background);
    check(tambyWithUser.stripe == tambyByAlias.stripe);
};

auto configuredColourWins = test("HostColours/configuredColourWins") = []
{
    auto config = AppConfig {};
    config.hosts["tamby"].color = "#123456";

    check(getColourBlindSafeHostRowColours(config, "jamiepond@tamby").stripe == Rgb {0x123456});
};

auto hexColoursParseStrictly = test("HostColours/hexColoursParseStrictly") = []
{
    check(parseHexColour("#eb6f92") == std::optional<Rgb> {0xeb6f92});
    check(!parseHexColour("eb6f92").has_value());
    check(!parseHexColour("#eb6f9").has_value());
    check(!parseHexColour("#eb6f9z").has_value());
};

auto hostLabelDropsTheUser = test("HostColours/hostLabelDropsTheUser") = []
{
    check(getHostLabel("parallels@ubuntu-vm") == "ubuntu-vm");
    check(getHostLabel("tamby") == "tamby");
};

auto blendingHalfwayMeetsInTheMiddle = test("HostColours/blendingHalfwayMeetsInTheMiddle") = []
{
    constexpr auto halfway = 0.5f;

    check(blendColours(0x000000, 0xfefefe, halfway) == Rgb {0x7f7f7f});
    check(blendColours(0x102030, 0xffffff, 0.0f) == Rgb {0x102030});
};

auto thisMachineTakesTheColourThePhoneGivesIt = test("HostColours/thisMachineTakesTheColourThePhoneGivesIt") = []
{
    auto config = AppConfig {};
    config.hostName = "big-cow";
    const auto phonesColours = getColourBlindSafeHostRowColours(config, "jamiepond@big-cow");

    check(getThisMachineColours(config).background == phonesColours.background);
    check(getThisMachineColours(config).stripe == phonesColours.stripe);
};

// The Mac reads cowterm.json; the phone reads only its "hosts", which pairing
// copies into host-colours.json. Each host must come out alike on both.
auto phoneShowsTheMacsConfiguredColours = test("HostColours/phoneShowsTheMacsConfiguredColours") = []
{
    const auto macConfig = parseConfig(R"({
        "hostName": "big-cow",
        "searchDirs": ["~/projects", "tamby:~/projects", "jamie@tamby-windows:~/projects"],
        "hosts": {"big-cow": {"color": "#56b4e9"}, "tamby": {"color": "#123456"},
                  "jamie@tamby-windows": {"color": "#654321"}}
    })");
    const auto phoneConfig = parseConfig(R"({"hosts": {"big-cow": {"color": "#56b4e9"},
        "tamby": {"color": "#123456"}, "jamie@tamby-windows": {"color": "#654321"}}})");

    const auto isAlike = [](const HostRowColours& mac, const HostRowColours& phone)
    { return mac.stripe == phone.stripe && mac.background == phone.background; };

    check(isAlike(getThisMachineColours(macConfig),
                  getColourBlindSafeHostRowColours(phoneConfig, "jamiepond@big-cow")));
    check(isAlike(getColourBlindSafeHostRowColours(macConfig, "tamby"),
                  getColourBlindSafeHostRowColours(phoneConfig, "jamiepond@tamby")));
    check(isAlike(getColourBlindSafeHostRowColours(macConfig, "jamie@tamby-windows"),
                  getColourBlindSafeHostRowColours(phoneConfig, "jamie@tamby-windows")));
    check(getThisMachineColours(macConfig).stripe == Rgb {0x56b4e9});
};
