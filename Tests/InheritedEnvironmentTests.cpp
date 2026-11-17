#include "InheritedEnvironment.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

auto tClaudeSessionMarkersAreNotInherited = test("InheritedEnvironment/claudeSessionMarkersAreNotInherited") = []
{
    check(isVariableAPaneMustNotInherit("CLAUDECODE"));
    check(isVariableAPaneMustNotInherit("CLAUDE_CODE_CHILD_SESSION"));
    check(isVariableAPaneMustNotInherit("CLAUDE_CODE_SESSION_ID"));
    check(isVariableAPaneMustNotInherit("CLAUDE_CODE_MESSAGING_TOKEN"));
    check(isVariableAPaneMustNotInherit("COWTERM_PANE"), "the pane gets its own");
};

auto tEverythingElseIsInherited = test("InheritedEnvironment/everythingElseIsInherited") = []
{
    check(!isVariableAPaneMustNotInherit("PATH"));
    check(!isVariableAPaneMustNotInherit("HOME"));
    check(!isVariableAPaneMustNotInherit("COWTERM_INSTANCE"));
    check(!isVariableAPaneMustNotInherit("MY_CLAUDE_NOTES"), "only names that start with CLAUDE");
};
