#pragma once

namespace term::cli::platform
{
// Connects ct's output to the terminal that ran it, where the OS leaves a GUI
// executable without one (Windows).
#ifdef _WIN32
void attachToTerminal();
#else
inline void attachToTerminal() {}
#endif
} // namespace term::cli::platform
