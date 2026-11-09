#include "HyperlinkTable.h"

#include <algorithm>
#include <array>

namespace term
{
namespace
{
constexpr auto openableSchemes =
    std::array<std::string_view, 4> {"http://", "https://", "mailto:", "file://"};
} // namespace

bool hasOpenableScheme(std::string_view uri)
{
    return std::any_of(openableSchemes.begin(),
                       openableSchemes.end(),
                       [uri](std::string_view scheme) { return uri.starts_with(scheme); });
}
} // namespace term
