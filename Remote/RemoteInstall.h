#pragma once

#include <string>
#include <vector>

namespace term::remote
{
struct RemoteInstallResult
{
    std::string host;
    bool succeeded = false;
    std::string output;
};

// The ssh command line that installs or updates ct + cowtermd on host.
std::vector<std::string> makeInstallCommand(const std::string& host);

// Runs scripts/remote-bootstrap.sh on host over ssh and waits. Blocking.
RemoteInstallResult installOnHost(const std::string& host);

// installOnHost for every host at once; results in the order given.
std::vector<RemoteInstallResult> installOnHosts(const std::vector<std::string>& hosts);

// "ct install-remote tamby", the fix an outdated or missing remote names.
std::string describeInstallFix(const std::string& host);
} // namespace term::remote
