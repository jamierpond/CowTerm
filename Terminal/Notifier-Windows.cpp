#include "Notifier.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/WinInclude.h>

#include <shellapi.h>

namespace term::Notifier
{
namespace
{
// Notifications ride on a dedicated Shell_NotifyIcon entry owned by a hidden
// message window (eacp's TrayIcon exposes no balloon API). New entries land
// in the taskbar overflow, so no extra icon shows up; Windows 10/11 render
// the balloons as toast notifications.
constexpr auto callbackMessage = UINT {WM_APP + 1};
constexpr auto iconId = UINT {1};

std::function<void(const std::string&)> activateHandler =
    [](const std::string&) {};

HWND window = nullptr;

// Balloons carry no payload, so a click maps to the session of the most
// recently posted notification — the one the balloon is showing.
std::string lastSessionKey;

NOTIFYICONDATAW makeIconData()
{
    auto data = NOTIFYICONDATAW {};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uID = iconId;
    return data;
}

LRESULT CALLBACK
wndProc(HWND target, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == callbackMessage && LOWORD(lParam) == NIN_BALLOONUSERCLICK)
    {
        eacp::Threads::callAsync([] { activateHandler(lastSessionKey); });
        return 0;
    }

    return DefWindowProcW(target, message, wParam, lParam);
}

HICON applicationIcon()
{
    wchar_t path[MAX_PATH] {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    auto* icon = ExtractIconW(GetModuleHandleW(nullptr), path, 0);

    if (icon != nullptr && icon != reinterpret_cast<HICON>(1))
        return icon;

    return LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
}
} // namespace

void initialize(std::function<void(const std::string&)> onActivate)
{
    activateHandler = std::move(onActivate);

    auto windowClass = WNDCLASSW {};
    windowClass.lpfnWndProc = wndProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"CowTermNotifierWindow";
    RegisterClassW(&windowClass);

    window = CreateWindowExW(0,
                             windowClass.lpszClassName,
                             L"",
                             0,
                             0,
                             0,
                             0,
                             0,
                             HWND_MESSAGE,
                             nullptr,
                             windowClass.hInstance,
                             nullptr);

    if (window == nullptr)
        return;

    auto data = makeIconData();
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = callbackMessage;
    data.hIcon = applicationIcon();
    wcsncpy_s(data.szTip, L"CowTerm", _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &data);

    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void notify(const std::string& sessionKey,
            const std::string& title,
            const std::string& body)
{
    if (window == nullptr)
        return;

    lastSessionKey = sessionKey;

    auto data = makeIconData();
    data.uFlags = NIF_INFO;
    wcsncpy_s(data.szInfoTitle,
              eacp::Strings::widen(title).c_str(),
              _TRUNCATE);
    wcsncpy_s(data.szInfo,
              eacp::Strings::widen(body).c_str(),
              _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}
} // namespace term::Notifier
