#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Claude Code's permission prompt, read off the pane's screen, so the phone can
// show its options as buttons above the key row (docs/iOSDesign.md, "Claude
// panes"). Plain logic, so the Mac's tests can check it with screen snapshots.
namespace cowterm::mobile
{
struct PromptOption
{
    int number = 0;
    std::string label;
};

struct PermissionPrompt
{
    std::string question;
    std::vector<PromptOption> options;

    // The prompt's own lines, the command it asks about among them: two
    // prompts with the same options for different commands differ here.
    std::string identity;
};

// The prompt on screen: a question ending in "?" followed by numbered options
// ("❯ 1. Yes", "2. No, and tell Claude what to do differently"), inside
// Claude's box or not. Empty when the screen shows none.
std::optional<PermissionPrompt> findPermissionPrompt(std::string_view screenText);

// What a tap on an option drawn for one prompt sends: the option's number, or
// nothing when the screen no longer shows that exact prompt. A late tap must
// never answer a different prompt.
std::optional<std::string> chooseKeysForTappedOption(const PermissionPrompt& drawnFor,
                                                     int optionNumber,
                                                     const std::optional<PermissionPrompt>& onScreen);
} // namespace cowterm::mobile
