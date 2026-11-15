#pragma once

#include "BridgeFraming.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace term::bridge
{
// The heart of `ct bridge`, without the I/O: bytes from the app (ssh's stdin)
// become daemon messages, daemon messages become framed bytes for the app.
class BridgeRelay
{
public:
    std::function<void(const std::string& message)> sendToDaemon = [](const std::string&) {};
    std::function<void(const std::string& bytes)> writeToApp = [](const std::string&) {};

    // The home directory remote paths starting with ~ are relative to.
    std::string homeDirectory;

    // False once the app's stream stops parsing; nothing more is relayed.
    bool receiveFromApp(std::string_view bytes);
    void receiveFromDaemon(const std::string& message);

    void announceReady(std::string_view ctVersion);
    void announceFailure(std::string_view reason);

private:
    // "scan" lists this machine's projects (the app's picker asks); the
    // daemon never sees it.
    std::optional<std::string> answerLocally(const std::string& message) const;

    FrameDecoder decoder;
};
} // namespace term::bridge
