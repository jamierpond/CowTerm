#include "PaneTerminalConfig.h"
#include "DaemonClient.h"
#include "Protocol.h"

namespace term
{
TerminalConfig terminalConfig(const AppConfig& config,
                              const std::string& host,
                              const std::string& sessionName)
{
    auto result = TerminalConfig {};
    result.font = config.font;
    result.fontSize = config.fontSize;
    result.theme = themeByName(config.theme);

    result.makeShell = [host, sessionName](const std::string& shellId)
    {
        if (host.empty())
            return makeLocalShell(shellId);

        return makeRemoteShell(shellId, host, sessionName);
    };

    // A remote pane's popup (Ctrl+A i) runs on that machine, in the daemon.
    if (!host.empty())
    {
        result.makeCommandShell = [host](const std::string& shellId)
        { return makeRemoteShell(shellId, host, {}); };
    }

    result.environmentForShell = [](const std::string& shellId)
    {
        return std::vector<EnvironmentVariable> {
            {proto::paneEnvironmentVariable, shellId}};
    };

    return result;
}
} // namespace term
