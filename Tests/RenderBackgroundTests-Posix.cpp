#include "RenderBackgroundTests.h"

const char* term::tests::getRedHelloCommand()
{
    return "printf '\\033[41mHELLO     \\033[0m'";
}

const char* term::tests::getPlainHelloCommand()
{
    return "printf 'HELLO'";
}
