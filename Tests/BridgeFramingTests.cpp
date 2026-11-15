#include "Multiplexer/BridgeFraming.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::bridge;

auto framesSurviveArbitrarySplits = test("BridgeFraming/framesSurviveArbitrarySplits") = []
{
    const auto first = std::string {"output p1\nline\none\0two", 22};
    const auto second = std::string {"info"};
    const auto stream = encodeFrame(first) + encodeFrame(second) + encodeFrame("");

    for (auto split = std::size_t {0}; split <= stream.size(); ++split)
    {
        auto decoder = FrameDecoder {};
        auto messages = decoder.append(std::string_view {stream}.substr(0, split));
        const auto rest = decoder.append(std::string_view {stream}.substr(split));
        messages.insert(messages.end(), rest.begin(), rest.end());

        check(messages == std::vector<std::string> {first, second, ""});
        check(!decoder.hasFailed());
    }
};

auto loginBannerFailsTheStream = test("BridgeFraming/loginBannerFailsTheStream") = []
{
    auto decoder = FrameDecoder {};
    const auto messages = decoder.append("Welcome to Ubuntu\n12\nbridge-hello");

    check(messages.empty());
    check(decoder.hasFailed());
};

auto endlessLengthLineFails = test("BridgeFraming/endlessLengthLineFails") = []
{
    auto decoder = FrameDecoder {};
    decoder.append(std::string(64, '7'));

    check(decoder.hasFailed());
};

auto helloRoundTrips = test("BridgeFraming/helloRoundTrips") = []
{
    const auto hello = parseHelloMessage(makeHelloMessage("abc1234"));

    check(hello.has_value());
    check(hello->protocolVersion == protocolVersion);
    check(hello->ctVersion == "abc1234");
    check(!parseHelloMessage("output p1").has_value());
};

auto errorCarriesItsReason = test("BridgeFraming/errorCarriesItsReason") = []
{
    const auto reason = parseErrorMessage(makeErrorMessage("cowtermd did not start"));

    check(reason == std::optional<std::string> {"cowtermd did not start"});
    check(!parseErrorMessage(makeHelloMessage("x")).has_value());
};
