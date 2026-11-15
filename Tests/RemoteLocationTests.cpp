#include "Multiplexer/RemoteLocation.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace term::remote;

auto userAtHostEntryIsRemote = test("RemoteLocation/userAtHostEntryIsRemote") = []
{
    const auto parsed = parseRemoteSearchDir("parallels@ubuntu-vm:~/projects");

    check(parsed.has_value());
    check(parsed->host == "parallels@ubuntu-vm");
    check(parsed->path == "~/projects");
};

auto bareHostWithoutPathMeansHome = test("RemoteLocation/bareHostWithoutPathMeansHome") = []
{
    const auto parsed = parseRemoteSearchDir("tamby:");

    check(parsed.has_value());
    check(parsed->host == "tamby");
    check(parsed->path == "~");
};

auto localEntriesStayLocal = test("RemoteLocation/localEntriesStayLocal") = []
{
    check(!parseRemoteSearchDir("~/projects").has_value());
    check(!parseRemoteSearchDir("/Users/x/a:b").has_value());
    check(!parseRemoteSearchDir("C:\\Users\\x").has_value());
    check(!parseRemoteSearchDir("./dir:x").has_value());
    check(!parseRemoteSearchDir("").has_value());
};

auto hostsAreCollectedOnceInOrder = test("RemoteLocation/hostsAreCollectedOnceInOrder") = []
{
    const auto hosts =
        collectRemoteHosts({"~/projects", "tamby:~/projects", "ubuntu-vm:/srv", "tamby:~"});

    check(hosts == std::vector<std::string> {"tamby", "ubuntu-vm"});
};
