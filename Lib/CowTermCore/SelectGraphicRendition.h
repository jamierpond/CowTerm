#pragma once

#include "CsiParameters.h"
#include "TermScreen.h"

namespace term
{
// SGR (CSI ... m): applies every parameter's attribute or colour to the pen.
void applySelectGraphicRendition(Pen& pen, const CsiParameters& parameters, const Theme& theme);
} // namespace term
