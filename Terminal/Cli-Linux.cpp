// ct on Linux. Nothing grants or refuses screen access per app, and there is
// no one capture tool across Wayland compositors and X11 to drive, so the
// capture commands report that rather than guessing. version, help and the
// recording state work as everywhere. The commands themselves are Cli.cpp.

#include "CliPlatform.h"

#include <eacp/Core/Utils/Files.h>

#include <cstdio>
#include <ctime>

namespace fs = std::filesystem;

namespace term::cli::platform
{
namespace
{
constexpr auto* unavailable = "screen capture is not available on Linux yet";
} // namespace

// A terminal launch already hands ct its stdio.
void attachToTerminal() {}

std::string timestamp()
{
    const auto now = std::time(nullptr);
    auto local = std::tm {};
    localtime_r(&now, &local);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y%m%d-%H%M%S", &local);
    return buffer;
}

bool captureGranted()
{
    return true;
}

bool requestCapture()
{
    std::printf("granted\n");
    return true;
}

void printGrantDetails()
{
    std::printf("app: %s\n", eacp::Files::executablePath().str().c_str());
}

Capture screenshot(const Options&, const fs::path&)
{
    return {1, unavailable};
}

std::optional<long long>
    startRecording(const Options&, const fs::path&, std::string& error)
{
    error = unavailable;
    return std::nullopt;
}

bool isRecorder(long long)
{
    return false;
}

void stopRecording(long long) {}

// No grant belongs to the hosting terminal here, so any shell will do.
bool insideCowTerm(std::string&)
{
    return true;
}
} // namespace term::cli::platform
