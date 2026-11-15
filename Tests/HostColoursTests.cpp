#include "HostColours.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto hostColourDependsOnlyOnTheName = test("HostColours/hostColourDependsOnlyOnTheName") = []
{
    const auto config = AppConfig {};

    check(getHostColour(config, "tamby") == getHostColour(config, "tamby"));
    check(getHostColour(config, "jamiepond@tamby") == getHostColour(config, "tamby"));
};

auto configuredColourWins = test("HostColours/configuredColourWins") = []
{
    auto config = AppConfig {};
    config.hosts["tamby"].color = "#123456";

    check(getHostColour(config, "jamiepond@tamby") == Rgb {0x123456});
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
