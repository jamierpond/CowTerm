#pragma once

#include <SessionLog/EventLine.h>

#include <initializer_list>
#include <string_view>

namespace term
{
// Moves CowTerm's old data into pond/CowTerm (logged as app.migrate), sends
// this launch's usage lines to pond/CowTerm/logs/<startEpochMs>.log and deletes
// all but the newest session logs. Call once, at launch.
void startUsageLog();

// One `<event> key=value ...` line about what the user did. Never pass pane
// contents or typed text.
void logUsage(std::string_view event, std::initializer_list<sessionlog::Field> fields = {});
} // namespace term
