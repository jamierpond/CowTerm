#include "RemoteInstall.h"

#include "RemoteBootstrapScript.h"

#include <eacp/Core/Process/Process.h>

#include <future>

namespace term::remote
{
std::vector<std::string> makeInstallCommand(const std::string& host)
{
    return {"ssh", "-o", "BatchMode=yes", host, "bash", "-l", "-s"};
}

RemoteInstallResult installOnHost(const std::string& host)
{
    const auto command = makeInstallCommand(host);

    auto options = eacp::Processes::ProcessOptions {};
    options.executable = command.front();
    options.arguments.assign(command.begin() + 1, command.end());
    options.mergeErrorOutput = true;

    auto process = eacp::Processes::Process {options};

    if (!process.launched())
        return {host, false, "could not run ssh"};

    process.writeToInput(remoteBootstrapScript);
    process.closeInput();
    const auto exitCode = process.wait();
    return {host, exitCode == 0, process.output()};
}

std::vector<RemoteInstallResult> installOnHosts(const std::vector<std::string>& hosts)
{
    auto pending = std::vector<std::future<RemoteInstallResult>> {};

    for (const auto& host: hosts)
        pending.push_back(std::async(std::launch::async, installOnHost, host));

    auto results = std::vector<RemoteInstallResult> {};

    for (auto& result: pending)
        results.push_back(result.get());

    return results;
}

std::string describeInstallFix(const std::string& host)
{
    return "ct install-remote " + host;
}
} // namespace term::remote
