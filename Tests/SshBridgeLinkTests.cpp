#include "SshBridgeLink.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term;

namespace
{
bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

constexpr int sshFailed = 255;
constexpr int commandNotFound = 127;
} // namespace

auto missingCtNamesTheInstallCommand = test("SshBridgeLink/missingCtNamesTheInstallCommand") = []
{
    const auto reason = describeBridgeFailure(
        "tamby", commandNotFound, "zsh:1: no such file or directory: /Users/x/.local/bin/ct\n");

    check(contains(reason, "ct install-remote tamby"));
};

auto refusedKeyIsAnAuthError = test("SshBridgeLink/refusedKeyIsAnAuthError") = []
{
    const auto reason =
        describeBridgeFailure("tamby", sshFailed, "x@tamby: Permission denied (publickey).\n");

    check(contains(reason, "refused key auth"));
};

auto unreachableHostSaysWhy = test("SshBridgeLink/unreachableHostSaysWhy") = []
{
    const auto reason = describeBridgeFailure(
        "tamby", sshFailed, "ssh: connect to host tamby port 22: Operation timed out\r\n");

    check(reason == "unreachable (ssh: connect to host tamby port 22: Operation timed out)");
};
