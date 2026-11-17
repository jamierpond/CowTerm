#include "InheritedEnvironment.h"

namespace term
{
bool isVariableAPaneMustNotInherit(std::string_view name)
{
    constexpr auto claudeSessionPrefix = std::string_view {"CLAUDE"};
    constexpr auto paneVariable = std::string_view {"COWTERM_PANE"};
    return name.starts_with(claudeSessionPrefix) || name == paneVariable;
}
} // namespace term
