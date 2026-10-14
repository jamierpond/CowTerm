#include "Permissions.h"

#include <eacp/Graphics/Graphics.h>

// Windows and Linux have no privacy grants for a terminal to hold: a command in
// a CowTerm shell captures the screen, drives other apps or reads the user's
// files without asking anyone. Every grant is in force, so the panel never
// opens by itself and there is nothing to request, reset or relaunch for.
namespace term::permissions::platform
{
Status check(Kind)
{
    return Status::granted;
}

void probeScreenRecording(std::function<void(bool granted)> done)
{
    done(true);
}

void request(Kind, bool, std::function<void()> done)
{
    done();
}

bool openSettings(Kind)
{
    return false;
}

bool startAppDrag(eacp::Graphics::View&)
{
    return false;
}

eacp::Graphics::Image appIcon()
{
    return {};
}

std::string bundleIdentifier()
{
    return {};
}

std::string bundlePath()
{
    return {};
}

bool reset(const std::string&, std::string& error)
{
    error = "this platform has no privacy grants to reset";
    return false;
}

void relaunch() {}
} // namespace term::permissions::platform
