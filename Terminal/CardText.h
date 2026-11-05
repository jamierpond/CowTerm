#pragma once

#include "AgentBoard.h"

#include <string>

namespace term
{
// The card's second line: empty when it would only repeat the title.
std::string getShownCardDescription(const TaskCard& card);

// The card's badge: empty when it would only repeat its column's name.
std::string getShownCardBadge(const TaskCard& card);
} // namespace term
