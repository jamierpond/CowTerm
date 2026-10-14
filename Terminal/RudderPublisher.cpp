#include "RudderPublisher.h"

#include "Projects.h"

#include <eacp/Core/Threads/Async.h>
#include <eacp/Core/Utils/FilePath.h>

namespace term
{
namespace
{
// A directory under ~/projects being created is often a clone or an unpack:
// a burst of events. Rescan once it settles.
constexpr auto placesRescanDelay = eacp::Time::MS {500};
} // namespace

RudderPublisher::RudderPublisher(const AppConfig& configToUse,
                                 SessionManager& managerToUse)
    : config(configToUse)
    , manager(managerToUse)
    , client(rudder::protocol::Hello {"CowTerm", "com.eacp.cowterm", 0})
{
    client.onActivate = [this](const std::string& contextId) { activate(contextId); };

    placesWatcher.onChange = [this] { schedulePlacesRescan(); };

    auto roots = std::vector<std::string> {};

    for (const auto& dir: config.searchDirs)
        roots.push_back(expandHome(dir));

    placesWatcher.watch(roots);
    rescanPlaces();
}

RudderPublisher::~RudderPublisher()
{
    *alive = false;
}

void RudderPublisher::schedulePush()
{
    if (pushPending)
        return;

    pushPending = true;
    eacp::Threads::callAsync(
        [this, guard = std::weak_ptr<bool> {alive}]
        {
            if (guard.expired())
                return;

            pushPending = false;
            push();
        });
}

void RudderPublisher::push()
{
    auto tabs = std::vector<RudderTab> {};

    for (const auto& session: manager.all())
    {
        auto tab = RudderTab {session->name, session->key(), {}};

        for (auto* pane: session->view.panes())
            tab.panes.push_back({pane->shellId(),
                                 pane->currentTitle(),
                                 pane->foregroundProcess(),
                                 pane->workingDirectory()});

        tabs.push_back(std::move(tab));
    }

    auto contexts =
        rudderContexts(tabs, places, eacp::FilePath::homeDirectory().str());

    if (contexts == pushed)
        return;

    pushed = contexts;
    client.pushContexts(std::move(contexts));
}

void RudderPublisher::activate(const std::string& contextId)
{
    const auto target = parseRudderContextId(contextId);

    if (!target)
        return;

    if (target->kind == RudderTarget::Kind::Place)
    {
        // Exactly what choosing it in ctrl+a f does: the session already on
        // that directory, or a new one.
        manager.openProject(target->value);
        onBringToFront();
        return;
    }

    for (const auto& session: manager.all())
        for (auto* pane: session->view.panes())
            if (pane->shellId() == target->value)
            {
                manager.switchTo(*session);
                session->view.focusPane(pane);
                onBringToFront();
                return;
            }
}

void RudderPublisher::schedulePlacesRescan()
{
    if (rescanPending)
        return;

    rescanPending = true;
    eacp::Threads::delay(placesRescanDelay)
        .then(
            [this, guard = std::weak_ptr<bool> {alive}]
            {
                if (guard.expired())
                    return;

                rescanPending = false;
                rescanPlaces();
            });
}

void RudderPublisher::rescanPlaces()
{
    auto paths = std::vector<std::string> {};

    for (const auto& project: scanProjects(config))
        paths.push_back(project.path);

    places = std::move(paths);
    schedulePush();
}
} // namespace term
