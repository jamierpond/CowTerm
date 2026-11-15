#pragma once

#include <string>
#include <vector>

// The remote-machine verbs shared by the app's `ct` and the headless `ct`.
namespace term::remote
{
// ct install-remote <host>
int runInstallRemoteCommand(const std::vector<std::string>& arguments);

// ct update-remotes: install-remote for every host in hosts, in parallel.
int runUpdateRemotesCommand(const std::vector<std::string>& hosts);
} // namespace term::remote
