#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// `ct bridge` relays the daemon's Messenger messages over ssh's stdin and
// stdout. Each message travels as its decimal byte length, a newline, then
// the bytes, so payloads may hold anything.
namespace term::bridge
{
// Bumped whenever the app and a remote `ct bridge` stop understanding each
// other; a mismatch tells the user to run `ct install-remote <host>`.
inline constexpr auto protocolVersion = 1;

std::string encodeFrame(std::string_view message);

class FrameDecoder
{
public:
    // The whole messages completed by bytes, in order.
    std::vector<std::string> append(std::string_view bytes);

    // A length line that isn't a number, or is absurdly large: the stream is
    // not a bridge (a login banner, a shell error) and nothing after it parses.
    bool hasFailed() const { return failed; }

    // What arrived before failing, for an error message.
    const std::string& getUnparsedBytes() const { return buffer; }

private:
    bool readOneFrame(std::vector<std::string>& messages);

    std::string buffer;
    std::size_t readOffset = 0;
    bool failed = false;
};

// The bridge's first message: "bridge-hello <protocolVersion> <ct version>".
std::string makeHelloMessage(std::string_view ctVersion);

struct BridgeHello
{
    int protocolVersion = 0;
    std::string ctVersion;
};

// Empty unless message is a hello.
std::optional<BridgeHello> parseHelloMessage(const std::string& message);

// Sent instead of a hello when the bridge can't reach its daemon.
std::string makeErrorMessage(std::string_view reason);
std::optional<std::string> parseErrorMessage(const std::string& message);
} // namespace term::bridge
