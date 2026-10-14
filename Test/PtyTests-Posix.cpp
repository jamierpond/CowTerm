#include "PtyTests.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::tests;

const char* const term::tests::idleCommand = "sleep 3";

// A shell that traps SIGHUP must not be able to wedge quit: shutdown escalates
// to an unignorable SIGKILL after a short grace period.
auto stubbornShell = test("Pty shutdown kills a shell that ignores SIGHUP") = []
{ check(shutsDownWithin(startShell("trap '' HUP; sleep 3"), budget)); };
