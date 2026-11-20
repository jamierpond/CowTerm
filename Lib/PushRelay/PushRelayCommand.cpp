#include "PushRelayCommand.h"
#include "PushRelay.h"
#include "PushRelayServer.h"

#include <eacp/Core/App/App.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <thread>

namespace term::push
{
namespace Apps = eacp::Apps;

namespace
{
constexpr int exitFail = 1;
constexpr auto timestampFormat = "%Y-%m-%d %H:%M:%S";
constexpr auto timestampBytes = std::size_t {32};

struct TextOption
{
    std::string_view flag;
    std::string PushRelayOptions::*field;
};

const auto textOptions = std::array {TextOption {"--listen", &PushRelayOptions::listenAddress},
                                     TextOption {"--topic", &PushRelayOptions::topic},
                                     TextOption {"--sound", &PushRelayOptions::soundName}};

std::tm getLocalTime(std::time_t time)
{
    auto localTime = std::tm {};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    return localTime;
}

void writeLogLine(const std::string& line)
{
    const auto localTime = getLocalTime(std::time(nullptr));
    auto timestamp = std::array<char, timestampBytes> {};
    std::strftime(timestamp.data(), timestamp.size(), timestampFormat, &localTime);
    std::printf("%s push-relay: %s\n", timestamp.data(), line.c_str());
    std::fflush(stdout);
}

std::int64_t getSecondsSinceEpoch()
{
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

PushRelayServices makeLiveServices()
{
    auto services = PushRelayServices {};
    services.sendToApple = eacp::HTTP::httpRequest;
    services.getSecondsSinceEpoch = getSecondsSinceEpoch;
    services.wait = [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); };
    services.log = writeLogLine;
    return services;
}

std::optional<int> readPort(const std::string& text)
{
    constexpr auto highestPort = 65535;

    try
    {
        const auto port = std::stoi(text);

        if (port > 0 && port <= highestPort)
            return port;
    }
    catch (const std::exception&)
    {
    }

    return std::nullopt;
}

std::string applyOption(PushRelayOptions& options, const std::string& flag, const std::string& value)
{
    if (const auto option = std::ranges::find(textOptions, flag, &TextOption::flag);
        option != textOptions.end())
    {
        options.*(option->field) = value;
        return {};
    }

    if (flag == "--devices")
    {
        options.deviceFile = value;
        return {};
    }

    if (flag != "--port")
        return "unknown option: " + flag;

    const auto port = readPort(value);

    if (!port)
        return "the port is a number from 1 to 65535";

    options.port = *port;
    return {};
}

struct PushRelayApp
{
    PushRelayApp(const PushRelayOptions& options, const ApnsSigningKey& signingKey)
        : relay(makeSettings(options, signingKey), makeLiveServices())
    {
        Apps::setDockIconVisible(false);

        if (!server.listen(options.listenAddress, options.port))
        {
            writeLogLine("cannot listen on " + options.listenAddress + ":" + std::to_string(options.port)
                         + " (is it this machine's tailnet address, and is the port free?)");
            Apps::quit(exitFail);
            return;
        }

        writeLogLine("listening on " + options.listenAddress + ":" + std::to_string(server.getBoundPort())
                     + " for " + options.topic + ", " + std::to_string(relay.getDevices().size())
                     + " phones registered, kept in " + options.deviceFile.string());
    }

    static PushRelaySettings makeSettings(const PushRelayOptions& options, const ApnsSigningKey& signingKey)
    {
        auto settings = PushRelaySettings {};
        settings.signingKey = signingKey;
        settings.topic = options.topic;
        settings.soundName = options.soundName;
        settings.deviceFile = options.deviceFile;
        return settings;
    }

    PushRelay relay;
    PushRelayServer server {relay};
};
} // namespace

ParsedPushRelayOptions parsePushRelayArguments(const std::vector<std::string>& args)
{
    constexpr auto argumentsPerOption = std::size_t {2};
    auto options = PushRelayOptions {};

    for (auto index = std::size_t {0}; index < args.size(); index += argumentsPerOption)
    {
        if (index + 1 >= args.size())
            return {std::nullopt, "missing a value after " + args[index]};

        if (const auto problem = applyOption(options, args[index], args[index + 1]); !problem.empty())
            return {std::nullopt, problem};
    }

    if (options.listenAddress.empty())
        return {std::nullopt, "--listen <address> is required: the relay host's tailnet address"};

    return {options, {}};
}

int runPushRelay(const PushRelayOptions& options, const ApnsSigningKey& signingKey)
{
    const auto check = createApnsProviderToken(signingKey, getSecondsSinceEpoch());

    if (!check.error.empty())
    {
        writeLogLine("cannot sign for Apple: " + check.error);
        return exitFail;
    }

    return Apps::run<PushRelayApp>(options, signingKey);
}
} // namespace term::push
