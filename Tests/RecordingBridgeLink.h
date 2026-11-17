#pragma once

#include "Multiplexer/BridgeLink.h"

#include <string>

// A BridgeLink with no transport: the test hands it the bridge's bytes and
// reads back what the link wrote.
class RecordingBridgeLink final : public term::BridgeLink
{
public:
    using BridgeLink::BridgeLink;

    void deliverBytes(const std::string& bytes) { receiveBytes(bytes); }
    void failTransport(const std::string& reason) { fail(reason); }

    std::string writtenBytes;
    bool isTransportStopped = false;

private:
    void writeBytes(const std::string& bytes) override { writtenBytes += bytes; }
    void stopTransport() override { isTransportStopped = true; }
};
