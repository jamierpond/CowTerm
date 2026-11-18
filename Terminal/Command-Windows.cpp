#include "Command.h"
#include "Multiplexer/UsageLog.h"

#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <shellapi.h>

namespace term
{
void openInBrowser(const std::string& url)
{
    logUsage("link.open", {{"url", url}});
    ShellExecuteW(
        nullptr, L"open", eacp::Strings::widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
} // namespace term
