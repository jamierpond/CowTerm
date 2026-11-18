#include "PaneViewing.h"

#include "Protocol.h"

namespace term
{
bool isSamePane(const ViewedPane& first, const ViewedPane& second)
{
    return first.host == second.host && first.paneId == second.paneId;
}

std::optional<ViewedPane> choosePhoneViewedPane(const PhoneAttention& attention)
{
    if (!attention.isPaneScreenShown || !attention.isInForeground)
        return std::nullopt;

    return attention.shownPane;
}

std::optional<ViewedPane> chooseDesktopViewedPane(const DesktopAttention& attention)
{
    const auto idleFor = attention.nowMilliseconds - attention.lastInputMilliseconds;

    if (!attention.isWindowFocused || idleFor >= desktopIdleMilliseconds)
        return std::nullopt;

    return attention.focusedPane;
}

PaneViewingReporter::PaneViewingReporter(SendToHost sendToUse)
    : send(std::move(sendToUse))
{
}

void PaneViewingReporter::setViewedPane(const std::optional<ViewedPane>& pane)
{
    const auto isUnchanged = pane.has_value() == viewed.has_value() && (!pane || isSamePane(*pane, *viewed));

    if (isUnchanged)
        return;

    if (viewed)
        send(viewed->host, proto::make("not-viewing", {viewed->paneId}));

    viewed = pane;

    if (viewed)
        sendViewing(*viewed);
}

void PaneViewingReporter::sendViewingAgainTo(const std::string& host)
{
    if (viewed && viewed->host == host)
        sendViewing(*viewed);
}

void PaneViewingReporter::sendViewing(const ViewedPane& pane)
{
    send(pane.host, proto::make("viewing", {pane.paneId}));
}
} // namespace term
