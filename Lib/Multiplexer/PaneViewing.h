#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// Which pane a person is looking at now, told to the daemon that runs it with
// `viewing <id>` and `not-viewing <id>`. A viewed pane is read, and a Claude
// finishing there while it is viewed neither marks it unread nor rings the phone.
namespace term
{
struct ViewedPane
{
    // Empty for this Mac's own daemon.
    std::string host;
    std::string paneId;
};

bool isSamePane(const ViewedPane& first, const ViewedPane& second);

// The phone views the pane its pane screen shows, while that screen is up and
// the app is in the foreground. dv's screen is not Claude's pane, so it views nothing.
struct PhoneAttention
{
    std::optional<ViewedPane> shownPane;
    bool isPaneScreenShown = false;
    bool isInForeground = true;
};

std::optional<ViewedPane> choosePhoneViewedPane(const PhoneAttention& attention);

// The Mac views the focused pane of its focused window, until a minute passes
// with no key typed or click there; the next one views it again.
constexpr auto desktopIdleMilliseconds = std::int64_t {60'000};

struct DesktopAttention
{
    std::optional<ViewedPane> focusedPane;
    bool isWindowFocused = false;
    std::int64_t lastInputMilliseconds = 0;
    std::int64_t nowMilliseconds = 0;
};

std::optional<ViewedPane> chooseDesktopViewedPane(const DesktopAttention& attention);

// Sends `viewing` and `not-viewing` only when the viewed pane changes, each to
// the daemon of its pane's host.
class PaneViewingReporter
{
public:
    using SendToHost = std::function<void(const std::string& host, const std::string& message)>;

    explicit PaneViewingReporter(SendToHost sendToUse);

    void setViewedPane(const std::optional<ViewedPane>& pane);

    // A host's link came back: its daemon forgot what this client was viewing.
    void sendViewingAgainTo(const std::string& host);

    const std::optional<ViewedPane>& getViewedPane() const { return viewed; }

private:
    void sendViewing(const ViewedPane& pane);

    SendToHost send;
    std::optional<ViewedPane> viewed;
};
} // namespace term
