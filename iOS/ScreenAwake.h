#pragma once

namespace cowterm::mobile
{
// Holds the auto-lock off (UIApplication's idleTimerDisabled) or lets it run.
// eacp has no call for it; this is the app's own glue.
void setScreenKeptAwake(bool isKeptAwake);
} // namespace cowterm::mobile
