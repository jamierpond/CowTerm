// ct on Windows. There is no grant to ask for -- any process may read the
// screen -- so ct captures from whatever shell runs it, with GDI for the
// pixels and eacp's Image for the PNG. Recording has no counterpart to macOS's
// screencapture to lean on, so it reports that rather than pretending. The
// commands themselves are Cli.cpp.

#include "CliPlatform.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Graphics/Image/Image.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace term::cli::platform
{
namespace
{
bool hasHandle(DWORD which)
{
    auto* handle = GetStdHandle(which);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE
           && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

struct Area
{
    RECT rect {};
    HWND window = nullptr;
};

std::vector<RECT> displays()
{
    auto found = std::vector<RECT> {};

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL
        {
            auto info = MONITORINFO {};
            info.cbSize = sizeof info;

            if (GetMonitorInfoW(monitor, &info))
            {
                auto& list = *reinterpret_cast<std::vector<RECT>*>(data);

                // Display 1 is the main one, as it is for screencapture -D.
                if ((info.dwFlags & MONITORINFOF_PRIMARY) != 0)
                    list.insert(list.begin(), info.rcMonitor);
                else
                    list.push_back(info.rcMonitor);
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));

    return found;
}

// --display, --window or --rect, else the main display. A window id is its
// HWND, in decimal or 0x hex.
bool areaFor(const Options& options, Area& area, std::string& error)
{
    if (!options.window.empty())
    {
        area.window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(
            std::strtoull(options.window.c_str(), nullptr, 0)));

        if (!IsWindow(area.window) || !GetWindowRect(area.window, &area.rect))
        {
            error = "no window " + options.window;
            return false;
        }

        return true;
    }

    if (!options.rect.empty())
    {
        auto x = 0L, y = 0L, width = 0L, height = 0L;

        if (sscanf_s(
                options.rect.c_str(), "%ld,%ld,%ld,%ld", &x, &y, &width, &height)
                != 4
            || width <= 0 || height <= 0)
        {
            error = "--rect wants x,y,w,h";
            return false;
        }

        area.rect = {x, y, x + width, y + height};
        return true;
    }

    const auto all = displays();
    const auto index =
        options.display.empty() ? 1 : std::atoi(options.display.c_str());

    if (index < 1 || index > (int) all.size())
    {
        error = "no display " + options.display;
        return false;
    }

    area.rect = all[(std::size_t) index - 1];
    return true;
}

// The area's pixels as RGBA. A window draws itself (PrintWindow), so one
// partly covered or off screen still comes out whole.
eacp::Graphics::Image grab(const Area& area)
{
    const auto width = (int) (area.rect.right - area.rect.left);
    const auto height = (int) (area.rect.bottom - area.rect.top);

    auto* screen = GetDC(nullptr);
    auto* memory = CreateCompatibleDC(screen);
    auto* bitmap = CreateCompatibleBitmap(screen, width, height);
    auto* previous = SelectObject(memory, bitmap);

    const auto drawn =
        area.window != nullptr
            ? PrintWindow(area.window, memory, PW_RENDERFULLCONTENT) != 0
            : BitBlt(memory,
                     0,
                     0,
                     width,
                     height,
                     screen,
                     area.rect.left,
                     area.rect.top,
                     SRCCOPY | CAPTUREBLT)
                  != 0;

    SelectObject(memory, previous);

    auto info = BITMAPINFO {};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    auto image = eacp::Graphics::Image {};
    auto* pixels = drawn ? image.prepareForOverwrite(width, height) : nullptr;

    if (pixels != nullptr
        && GetDIBits(memory, bitmap, 0, (UINT) height, pixels, &info, DIB_RGB_COLORS)
               == height)
    {
        // GDI hands back BGRx with the x undefined; Image wants opaque RGBA.
        for (auto i = std::size_t {0}; i < (std::size_t) width * height * 4; i += 4)
        {
            std::swap(pixels[i], pixels[i + 2]);
            pixels[i + 3] = 255;
        }
    }
    else
    {
        image = {};
    }

    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return image;
}
} // namespace

// CowTerm.exe is a GUI executable, so Windows starts it with no console: run
// from a shell, its output would go nowhere. Borrow the shell's console for
// whichever streams weren't redirected; a pipe (an agent reading the output)
// is already in place and stays.
void attachToTerminal()
{
    const auto needsOutput = !hasHandle(STD_OUTPUT_HANDLE);
    const auto needsError = !hasHandle(STD_ERROR_HANDLE);

    if ((!needsOutput && !needsError) || !AttachConsole(ATTACH_PARENT_PROCESS))
        return;

    auto* stream = (FILE*) nullptr;

    if (needsOutput)
        freopen_s(&stream, "CONOUT$", "w", stdout);

    if (needsError)
        freopen_s(&stream, "CONOUT$", "w", stderr);
}

std::string timestamp()
{
    const auto now = std::time(nullptr);
    auto local = std::tm {};
    localtime_s(&local, &now);
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
    std::printf("signature: not needed (Windows keys no grant on it)\n");
}

Capture screenshot(const Options& options, const fs::path& path)
{
    if (options.interactive)
        return {1, "--interactive is not available on Windows"};

    // Physical pixels, not a 96-DPI view of a scaled display.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto area = Area {};
    auto error = std::string {};

    if (!areaFor(options, area, error))
        return {1, error};

    const auto image = grab(area);

    if (!image)
        return {1, "could not read the screen"};

    try
    {
        image.save(eacp::FilePath {path});
    }
    catch (const std::exception& failure)
    {
        return {1, failure.what()};
    }

    return {0, {}};
}

std::optional<long long>
    startRecording(const Options&, const fs::path&, std::string& error)
{
    error = "screen recording is not available on Windows yet";
    return std::nullopt;
}

bool isRecorder(long long)
{
    return false;
}

void stopRecording(long long) {}

// Windows charges no grant to the terminal hosting the shell, so there is no
// wrong app to ask on behalf of: ct captures from any shell.
bool insideCowTerm(std::string&)
{
    return true;
}
} // namespace term::cli::platform
