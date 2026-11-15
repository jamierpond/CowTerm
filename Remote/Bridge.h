#pragma once

#include <string>

namespace term::bridge
{
// `ct bridge`: connects to this machine's session daemon (starting it if
// needed) and relays its messages over stdin/stdout for CowTerm on another
// machine, which runs it through ssh. Returns when stdin closes.
int runBridge(const std::string& ctVersion);
} // namespace term::bridge
