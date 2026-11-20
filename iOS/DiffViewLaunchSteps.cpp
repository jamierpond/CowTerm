#include "DiffViewLaunchSteps.h"

namespace cowterm::mobile
{
namespace
{
constexpr auto stepSeparator = ';';
constexpr auto argumentSeparator = ':';

struct NamedAction
{
    std::string_view name;
    DiffViewLaunchAction action;
};

constexpr NamedAction namedActions[] = {
    {"keyboard", DiffViewLaunchAction::RaiseKeyboard},
    {"tap", DiffViewLaunchAction::TapText},
    {"type", DiffViewLaunchAction::TypeText},
    {"key", DiffViewLaunchAction::PressKeyRowKey},
};

struct NamedKey
{
    std::string_view name;
    KeyRowKey key;
};

constexpr NamedKey namedKeys[] = {
    {"esc", KeyRowKey::Escape},
    {"tab", KeyRowKey::Tab},
    {"ctrl", KeyRowKey::Control},
    {"up", KeyRowKey::Up},
    {"down", KeyRowKey::Down},
    {"left", KeyRowKey::Left},
    {"right", KeyRowKey::Right},
};

std::optional<DiffViewLaunchAction> findActionNamed(std::string_view name)
{
    for (const auto& named: namedActions)
    {
        if (named.name == name)
            return named.action;
    }

    return std::nullopt;
}

std::optional<KeyRowKey> findKeyNamed(std::string_view name)
{
    for (const auto& named: namedKeys)
    {
        if (named.name == name)
            return named.key;
    }

    return std::nullopt;
}

std::optional<DiffViewLaunchStep> parseDiffViewLaunchStep(std::string_view text)
{
    const auto separatorAt = text.find(argumentSeparator);
    const auto name = text.substr(0, separatorAt);
    const auto argument = [&]
    {
        if (separatorAt == std::string_view::npos)
            return std::string_view {};

        return text.substr(separatorAt + 1);
    }();

    const auto action = findActionNamed(name);

    if (!action)
        return std::nullopt;

    auto step = DiffViewLaunchStep {*action, std::string {argument}};

    if (step.action != DiffViewLaunchAction::PressKeyRowKey)
        return step;

    const auto key = findKeyNamed(argument);

    if (!key)
        return std::nullopt;

    step.key = *key;
    return step;
}

bool isUtf8ContinuationByte(char byte)
{
    constexpr auto continuationMask = 0xC0;
    constexpr auto continuationBits = 0x80;
    const auto value = static_cast<unsigned char>(byte);
    return (value & continuationMask) == continuationBits;
}

int countCharacters(std::string_view text)
{
    auto count = 0;

    for (const auto byte: text)
    {
        if (!isUtf8ContinuationByte(byte))
            ++count;
    }

    return count;
}

std::vector<std::string_view> splitIntoLines(std::string_view text)
{
    auto lines = std::vector<std::string_view> {};

    while (!text.empty())
    {
        const auto lineEnd = text.find('\n');
        lines.push_back(text.substr(0, lineEnd));

        if (lineEnd == std::string_view::npos)
            break;

        text.remove_prefix(lineEnd + 1);
    }

    return lines;
}
} // namespace

std::vector<DiffViewLaunchStep> parseDiffViewLaunchSteps(std::string_view steps)
{
    auto parsed = std::vector<DiffViewLaunchStep> {};

    while (!steps.empty())
    {
        const auto stepEnd = steps.find(stepSeparator);

        if (const auto step = parseDiffViewLaunchStep(steps.substr(0, stepEnd)))
            parsed.push_back(*step);

        if (stepEnd == std::string_view::npos)
            break;

        steps.remove_prefix(stepEnd + 1);
    }

    return parsed;
}

std::optional<ScreenTextPosition> findLastTextOnScreen(const std::string& screenText,
                                                       const std::string& text)
{
    const auto lines = splitIntoLines(screenText);

    for (auto row = static_cast<int>(lines.size()) - 1; row >= 0; --row)
    {
        const auto line = lines[static_cast<std::size_t>(row)];
        const auto foundAt = line.rfind(text);

        if (foundAt != std::string_view::npos)
        {
            const auto startColumn = countCharacters(line.substr(0, foundAt));
            return ScreenTextPosition {row, startColumn + countCharacters(text) / 2};
        }
    }

    return std::nullopt;
}

std::vector<std::string> splitIntoCharacters(std::string_view text)
{
    auto characters = std::vector<std::string> {};

    for (const auto byte: text)
    {
        if (isUtf8ContinuationByte(byte) && !characters.empty())
            characters.back() += byte;
        else
            characters.emplace_back(1, byte);
    }

    return characters;
}
} // namespace cowterm::mobile
