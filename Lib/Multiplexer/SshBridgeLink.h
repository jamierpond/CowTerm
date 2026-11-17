#pragma once

#include "BridgeLink.h"

#include <memory>
#include <string>

namespace eacp::Processes
{
class Process;
}

namespace term
{
// A remote session daemon, reached by running `ct bridge` on host through
// the system ssh (key auth only: BatchMode, no prompts).
class SshBridgeLink final : public BridgeLink
{
public:
    explicit SshBridgeLink(std::string hostToUse);
    ~SshBridgeLink() override;

private:
    void writeBytes(const std::string& bytes) override;
    void stopTransport() override;
    void handleExit(int exitCode, const std::string& errorOutput);

    std::shared_ptr<eacp::Processes::Process> process;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// The Mac app's BridgeLinkFactory: the system ssh.
std::unique_ptr<BridgeLink> makeSshBridgeLink(const std::string& host);
} // namespace term
