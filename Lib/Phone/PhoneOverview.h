#pragma once

#include "Multiplexer/SessionOverview.h"

#include <eacp/Core/Threads/Timer.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace term::phone
{
struct UnreachableHostReport
{
    std::string host;
    std::string reason;
};

// What the overview screen draws: every session on every host that answers,
// and every host that does not, with why.
struct OverviewReport
{
    std::vector<overview::OverviewRow> sessions;
    std::vector<UnreachableHostReport> unreachableHosts;
    int hostCount = 0;
    std::int64_t nowMillisecondsSinceEpoch = 0;
};

// The overview's rows, from each host's daemon (RemoteHosts): while the
// overview is shown it asks every connected daemon for `info` and `activity`
// every few seconds and reports the rows each time. A daemon from before
// `activity` still gives rows from `info`, with no state line. Main-thread object.
class PhoneOverview
{
public:
    explicit PhoneOverview(std::vector<std::string> hostsToUse);

    // Asking starts at once when the overview is shown and stops when it is not.
    void setShown(bool isShownNow);
    bool isShown() const { return shown; }

    // Reports what is known now, then asks every connected daemon again.
    void refresh();

    // A host's daemon answered `info` or `activity`.
    void handleInfoChanged();
    void handleActivity(const std::string& host, const std::string& payload);

    // The pane a row opens: of its panes the daemon lists, the one typed into
    // last, else the first. Empty when the daemon lists none of them.
    std::optional<std::string> findPaneToOpen(const overview::OverviewRow& row) const;

    OverviewReport buildReport() const;

    std::function<void(const OverviewReport&)> onRowsChanged = [](const OverviewReport&) {};

private:
    overview::HostPanes collectHostPanes(const std::string& host) const;
    overview::PaneActivityReport findActivity(const std::string& host, const std::string& paneId) const;
    void report();

    std::vector<std::string> hosts;
    std::map<std::string, std::vector<overview::PaneActivityReport>> activityByHost;
    bool shown = false;
    std::unique_ptr<eacp::Threads::Timer> refreshTimer;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
} // namespace term::phone
