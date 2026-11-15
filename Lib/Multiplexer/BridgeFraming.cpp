#include "BridgeFraming.h"

#include "Protocol.h"

#include <charconv>

namespace term::bridge
{
namespace
{
constexpr auto maximumFrameBytes = std::size_t {512} * 1024 * 1024;
constexpr auto maximumLengthLineBytes = std::size_t {20};
constexpr auto helloVerb = "bridge-hello";
constexpr auto errorVerb = "bridge-error";

std::optional<std::size_t> parseFrameLength(std::string_view line)
{
    auto length = std::size_t {0};
    const auto* end = line.data() + line.size();
    const auto [stop, error] = std::from_chars(line.data(), end, length);

    const auto isWholeNumber = !line.empty() && error == std::errc {} && stop == end;

    if (!isWholeNumber || length > maximumFrameBytes)
        return std::nullopt;

    return length;
}
} // namespace

std::string encodeFrame(std::string_view message)
{
    auto frame = std::to_string(message.size());
    frame += '\n';
    frame += message;
    return frame;
}

std::vector<std::string> FrameDecoder::append(std::string_view bytes)
{
    auto messages = std::vector<std::string> {};

    if (failed)
        return messages;

    buffer += bytes;

    while (readOneFrame(messages))
    {
    }

    buffer.erase(0, readOffset);
    readOffset = 0;
    return messages;
}

bool FrameDecoder::readOneFrame(std::vector<std::string>& messages)
{
    const auto lineEnd = buffer.find('\n', readOffset);

    if (lineEnd == std::string::npos)
    {
        failed = buffer.size() - readOffset > maximumLengthLineBytes;
        return false;
    }

    const auto length = parseFrameLength(
        std::string_view {buffer}.substr(readOffset, lineEnd - readOffset));

    if (!length)
    {
        failed = true;
        return false;
    }

    const auto bodyStart = lineEnd + 1;

    if (buffer.size() - bodyStart < *length)
        return false;

    messages.push_back(buffer.substr(bodyStart, *length));
    readOffset = bodyStart + *length;
    return true;
}

std::string makeHelloMessage(std::string_view ctVersion)
{
    return proto::make(helloVerb, {std::to_string(protocolVersion), ctVersion});
}

std::optional<BridgeHello> parseHelloMessage(const std::string& message)
{
    const auto parsed = proto::parse(message);

    if (parsed.verb != helloVerb || parsed.args.empty())
        return std::nullopt;

    auto hello = BridgeHello {};
    const auto& version = parsed.args[0];
    const auto [stop, error] =
        std::from_chars(version.data(), version.data() + version.size(), hello.protocolVersion);

    if (error != std::errc {})
        return std::nullopt;

    if (parsed.args.size() > 1)
        hello.ctVersion = parsed.args[1];

    return hello;
}

std::string makeErrorMessage(std::string_view reason)
{
    return proto::make(errorVerb, {}, reason);
}

std::optional<std::string> parseErrorMessage(const std::string& message)
{
    const auto parsed = proto::parse(message);

    if (parsed.verb != errorVerb)
        return std::nullopt;

    return parsed.payload;
}
} // namespace term::bridge
