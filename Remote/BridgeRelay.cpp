#include "BridgeRelay.h"

namespace term::bridge
{
bool BridgeRelay::receiveFromApp(std::string_view bytes)
{
    for (const auto& message: decoder.append(bytes))
        sendToDaemon(message);

    return !decoder.hasFailed();
}

void BridgeRelay::receiveFromDaemon(const std::string& message)
{
    writeToApp(encodeFrame(message));
}

void BridgeRelay::announceReady(std::string_view ctVersion)
{
    writeToApp(encodeFrame(makeHelloMessage(ctVersion)));
}

void BridgeRelay::announceFailure(std::string_view reason)
{
    writeToApp(encodeFrame(makeErrorMessage(reason)));
}
} // namespace term::bridge
