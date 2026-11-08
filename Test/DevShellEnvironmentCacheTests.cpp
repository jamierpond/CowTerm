#include "DevShellEnvironmentCache.h"

#include <NanoTest/NanoTest.h>

// The cached Visual Studio developer environment a new PowerShell pane starts
// with: the file format, and when a cache stops matching the install.

using namespace nano;
using namespace term;

namespace
{
const auto install = VisualStudioInstall {
    "C:\\Program Files\\Microsoft Visual Studio\\18\\Community", "18.0.11111.16", "arm64"};

DevShellEnvironmentCache makeCache()
{
    return {install,
            {{"PATH", "C:\\VS\\bin;C:\\Windows"},
             {"INCLUDE", "C:\\VS\\include"},
             {"VSCMD_ARG_TGT_ARCH", "arm64"}}};
}

bool isSameVariables(const std::vector<EnvironmentVariable>& left,
                     const std::vector<EnvironmentVariable>& right)
{
    if (left.size() != right.size())
        return false;

    for (auto index = std::size_t {0}; index < left.size(); ++index)
    {
        if (left[index].name != right[index].name
            || left[index].value != right[index].value)
        {
            return false;
        }
    }

    return true;
}
} // namespace

auto roundTrip = test("DevShell cache reads back what it wrote") = []
{
    const auto parsed =
        parseDevShellEnvironmentCache(serialiseDevShellEnvironmentCache(makeCache()));

    check(parsed.has_value());
    check(isDevShellEnvironmentCacheFor(*parsed, install));
    check(isSameVariables(parsed->variables, makeCache().variables));
};

auto valueWithEquals = test("DevShell cache keeps '=' inside a value") = []
{
    const auto variables = parseEnvironmentVariableLines("OPTS=a=b\r\n=C:=C:\\\nnoequals\n");

    check(variables.size() == 1);
    check(variables[0].name == "OPTS");
    check(variables[0].value == "a=b");
};

auto crlf = test("DevShell cache parses a file with CRLF line ends") = []
{
    const auto text =
        "installationPath=C:\\VS\r\ninstallationVersion=1.2\r\narchitecture=x64\r\n\r\n"
        "LIB=C:\\VS\\lib\r\n";
    const auto parsed = parseDevShellEnvironmentCache(text);

    check(parsed.has_value());
    check(parsed->install.installationPath == "C:\\VS");
    check(parsed->variables.size() == 1);
    check(parsed->variables[0].value == "C:\\VS\\lib");
};

auto missingField = test("DevShell cache without a version is no cache") = []
{
    check(!parseDevShellEnvironmentCache("installationPath=C:\\VS\narchitecture=x64\n\nA=1\n")
               .has_value());
    check(!parseDevShellEnvironmentCache("").has_value());
};

auto versionChange = test("DevShell cache is stale after a Visual Studio update") = []
{
    auto updated = install;
    updated.installationVersion = "18.1.0.0";
    check(!isDevShellEnvironmentCacheFor(makeCache(), updated));
};

auto pathChange = test("DevShell cache is stale for another install") = []
{
    auto other = install;
    other.installationPath = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community";
    check(!isDevShellEnvironmentCacheFor(makeCache(), other));
};

auto architectureChange = test("DevShell cache is stale for another architecture") = []
{
    auto other = install;
    other.architecture = "x64";
    check(!isDevShellEnvironmentCacheFor(makeCache(), other));
};

auto emptyCapture = test("DevShell cache with no variables is never used") = []
{
    check(!isDevShellEnvironmentCacheFor({install, {}}, install));
};

auto vswhere = test("vswhere text output gives the install path and version") = []
{
    const auto output = "instanceId: 3f2a\r\n"
                        "installationName: VisualStudio/18.0.0\r\n"
                        "installationPath: C:\\Program Files\\Microsoft Visual "
                        "Studio\\18\\Community\r\n"
                        "installationVersion: 18.0.11111.16\r\n"
                        "isPrerelease: 0\r\n";
    const auto parsed = parseVswhereOutput(output, "arm64");

    check(parsed.has_value());
    check(parsed->installationPath == install.installationPath);
    check(parsed->installationVersion == install.installationVersion);
    check(parsed->architecture == "arm64");
};

auto vswhereEmpty = test("vswhere with no install found gives nothing") = []
{ check(!parseVswhereOutput("", "x64").has_value()); };
