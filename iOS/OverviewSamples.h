#pragma once

#include "OverviewLayout.h"

// Sessions on three machines, two of them waiting and one machine down, for
// the overview launched with --overview before the daemons feed it.
namespace cowterm::mobile
{
OverviewInputs makeSampleOverviewInputs();
} // namespace cowterm::mobile
