#pragma once

#include "Multiplexer/BridgeLink.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

// Hands RemoteHost (or PhoneConnection) a RecordingBridgeLink and keeps a handle on it.
struct RecordingLinkFactory
{
    term::BridgeLinkFactory makeFactory()
    {
        return [this](const std::string& requestedHost)
        {
            requestedHosts.push_back(requestedHost);
            auto link = std::make_unique<RecordingBridgeLink>(requestedHost);
            lastLink = std::ref(*link);
            return link;
        };
    }

    RecordingBridgeLink& getLastLink() { return lastLink->get(); }

    std::vector<std::string> requestedHosts;
    std::optional<std::reference_wrapper<RecordingBridgeLink>> lastLink;
};
