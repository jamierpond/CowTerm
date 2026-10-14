#include "RenderBackgroundTests.h"

// Straight to the console with [Console]::Write: Write-Host would pass the text
// through PowerShell's own rendering, which strips escape sequences. And no
// double quotes, because the Pty hands the command to PowerShell inside a pair
// of its own -- hence ESC as [char]27 joined in rather than "`e".
const char* const term::tests::redHelloCommand =
    "[Console]::Write(([char]27, '[41mHELLO     ', [char]27, '[0m') -join '')";

const char* const term::tests::plainHelloCommand = "[Console]::Write('HELLO')";
