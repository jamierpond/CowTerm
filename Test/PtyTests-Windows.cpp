#include "PtyTests.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::tests;

// Commands run through PowerShell (cmd only when it is missing).
const char* const term::tests::idleCommand = "Start-Sleep -Seconds 3";

// ConPTY's own hazard: ClosePseudoConsole waits for the console to flush its
// output, so with a shell writing flat out and nobody draining the pipe it
// never returns. shutdown() closes the console while the reader is still
// draining, so a flood must tear down as promptly as silence.
auto floodingShell = test("Pty shutdown returns while the shell floods output") = []
{ check(shutsDownWithin(startShell("while ($true) { 'x' * 200 }"), budget)); };
