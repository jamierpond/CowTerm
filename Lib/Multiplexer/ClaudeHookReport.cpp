#include "ClaudeHookReport.h"

#include <Miro/Reflect.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <string_view>

namespace term::cli
{
namespace
{
struct ClaudeHookState
{
    std::string_view hookEventName;
    std::string_view state;
};

constexpr auto claudeHookStates = std::array {ClaudeHookState {"UserPromptSubmit", "working"},
                                              ClaudeHookState {"Notification", "waiting"},
                                              ClaudeHookState {"Stop", "finished"}};

bool isBlank(std::string_view line)
{
    return line.find_first_not_of(" \t\r") == std::string_view::npos;
}

std::string findLastNonBlankLine(const std::string& text)
{
    auto lines = std::istringstream {text};
    auto lastLine = std::string {};

    for (auto line = std::string {}; std::getline(lines, line);)
    {
        if (!isBlank(line))
            lastLine = line;
    }

    const auto firstVisible = lastLine.find_first_not_of(" \t");
    const auto lastVisible = lastLine.find_last_not_of(" \t\r");

    if (firstVisible == std::string::npos)
        return {};

    return lastLine.substr(firstVisible, lastVisible - firstVisible + 1);
}

// The fields of Claude Code's hook input that say what Claude is doing.
struct ClaudeHookInput
{
    std::string hookEventName;
    std::string message;
    std::string lastAssistantMessage;

    MIRO_REFLECT_MEMBERS(hookEventName,
                         "hook_event_name",
                         message,
                         "message",
                         lastAssistantMessage,
                         "last_assistant_message")
};
} // namespace

std::optional<PaneStateReport> readClaudeHookReport(const std::string& hookInputJson,
                                                    const std::string& paneId)
{
    const auto input = Miro::createFromJSONString<ClaudeHookInput>(hookInputJson);
    const auto found =
        std::ranges::find(claudeHookStates, input.hookEventName, &ClaudeHookState::hookEventName);

    if (found == claudeHookStates.end())
        return std::nullopt;

    auto report = PaneStateReport {paneId, std::string {found->state}, {}};

    if (input.hookEventName == "Notification")
        report.message = input.message;
    else if (input.hookEventName == "Stop")
        report.message = findLastNonBlankLine(input.lastAssistantMessage);

    return report;
}
} // namespace term::cli
