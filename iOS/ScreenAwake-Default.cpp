#include "ScreenAwake.h"

namespace cowterm::mobile
{
// Off the phone, where the pane screen runs in tests, there is no auto-lock.
void setScreenKeptAwake(bool) {}
} // namespace cowterm::mobile
