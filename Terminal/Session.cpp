#include "Session.h"
#include "Multiplexer/UsageLog.h"
#include "Command.h"
#include "Multiplexer/DataDirectories.h"
#include "Multiplexer/Projects.h"
#include "Multiplexer/HostColours.h"
#include "Multiplexer/RemoteHosts.h"
#include "Multiplexer/RemoteLocation.h"

#include <eacp/Core/Threads/Async.h>
#include <emberstore/AppDatabase.h>

#include <algorithm>

namespace term
{
namespace
{
constexpr auto persistDelay = eacp::Time::MS {500};

std::string getProjectHost(const std::string& projectDir)
{
    const auto remote = remote::parseRemoteSearchDir(projectDir);
    return remote ? remote->host : std::string {};
}

DaemonClient* findDaemonOfHost(const std::string& host)
{
    if (host.empty())
        return DaemonClient::get();

    return RemoteHosts::findDaemon(host);
}

// The directory a session's first pane starts in: a remote project's path
// on its host, or the project itself.
std::string getStartDirectory(const std::string& projectDir, const std::string& startCwd)
{
    if (!startCwd.empty())
        return startCwd;

    if (const auto remote = remote::parseRemoteSearchDir(projectDir))
        return remote->path;

    return projectDir;
}
} // namespace

TermSession::TermSession(const AppConfig& config,
                         std::string nameToUse,
                         std::string projectDirToUse,
                         const std::string& startCwd)
    : name(std::move(nameToUse))
    , projectDir(std::move(projectDirToUse))
    , host(getProjectHost(projectDir))
    , view(config, getStartDirectory(projectDir, startCwd), {host, name})
{
}

std::string TermSession::activeTitle() const
{
    const auto* pane = view.activePane();
    return pane != nullptr ? pane->currentTitle() : std::string {};
}

std::vector<std::string> TermSession::listShellIds() const
{
    auto ids = std::vector<std::string> {};

    for (const auto* pane: view.panes())
        ids.push_back(pane->shellId());

    return ids;
}

std::string TermSession::activeWorkingDirectory() const
{
    const auto* pane = view.activePane();

    if (pane != nullptr && !pane->workingDirectory().empty())
        return pane->workingDirectory();

    return getStartDirectory(projectDir, {});
}

std::string TermSession::getActiveDirectoryAsProjectDir() const
{
    if (!isRemote())
        return activeWorkingDirectory();

    return remote::makeRemoteProjectDir(host, activeWorkingDirectory());
}

namespace
{
} // namespace

SessionManager::SessionManager(const AppConfig& configToUse)
    : config(configToUse)
    , db(eacp::FilePath {getSessionStoreDirectory().string()}, emberstore::Durability::Atomic)
    , mru(db)
    , paletteUsage(db)
    , saved(db.document<SavedState>("sessions"))
{
}

TermSession* SessionManager::find(const std::string& key)
{
    for (auto& session: sessions)
        if (session->key() == key)
            return session.get();

    return nullptr;
}

bool SessionManager::showPane(const std::string& shellId)
{
    for (const auto& session: sessions)
    {
        for (auto* pane: session->view.panes())
        {
            if (pane->shellId() == shellId)
            {
                switchTo(*session);
                session->view.focusPane(pane);
                return true;
            }
        }
    }

    return false;
}

std::string SessionManager::uniqueName(const std::string& base) const
{
    auto name = base;
    auto suffix = 2;

    auto taken = [&]
    {
        for (auto& session: sessions)
            if (session->name == name)
                return true;

        return false;
    };

    while (taken())
        name = base + " " + std::to_string(suffix++);

    return name;
}

void SessionManager::wireSession(TermSession& session)
{
    auto* raw = &session;

    session.view.onPaneCreated = [this, raw](TerminalView& pane)
    {
        pane.onNotify = [this, raw](const std::string& text)
        {
            raw->lastNotify = text;
            onNotify(*raw, text);
        };

        pane.onOpenHyperlink = [](const std::string& uri) { openInBrowser(uri); };

        pane.onCwdChanged = [this](const std::string&)
        {
            persist();
            onPanesChanged();
        };

        onPaneWired(*raw, pane);
    };

    // The last pane closing ends the session. Deferred: onEmpty fires from
    // inside the session view, and close() destroys it.
    session.view.onEmpty = [this, raw]
    { eacp::Threads::callAsync([this, raw] { closeIfPresent(raw); }); };

    session.view.onActivePaneChanged = [this, raw]
    {
        if (activeSession == raw)
            onActiveChanged(*raw);

        persist();
        onPanesChanged();
    };
}

TermSession& SessionManager::createSession(const std::string& name,
                                           const std::string& projectDir,
                                           const std::string& startCwd,
                                           const std::vector<SavedPane>& panes)
{
    auto& session = *sessions.emplace_back(std::make_unique<TermSession>(
        config, uniqueName(name), normalizedDir(projectDir), startCwd));
    wireSession(session);
    session.view.restore(panes);
    onSessionsChanged();
    return session;
}

TermSession& SessionManager::openProject(const std::string& dir)
{
    const auto path = normalizedDir(dir);

    if (auto* existing = find(path))
    {
        switchTo(*existing);
        return *existing;
    }

    auto& session = createSession(getSessionNameForProject(path), path, {}, {});
    logUsage("session.open", {{"name", session.name}, {"via", "project"}});
    switchTo(session);
    return session;
}

TermSession& SessionManager::newSession(const std::string& dir)
{
    const auto path = normalizedDir(dir);
    auto& session = createSession(getSessionNameForProject(path), path, {}, {});
    logUsage("session.open", {{"name", session.name}, {"via", "new"}});
    switchTo(session);
    return session;
}

void SessionManager::adoptRemoteSession(const std::string& layout)
{
    auto session = SavedSession {};
    Miro::fromJSONString(session, layout);

    if (session.name.empty() || find(normalizedDir(session.projectDir)) != nullptr)
        return;

    for (const auto& existing: sessions)
    {
        if (existing->name == session.name)
            return;
    }

    createSession(session.name, session.projectDir, session.cwd, session.panes);
    logUsage("session.open", {{"name", session.name}, {"via", "remote"}});
    persist();
}

void SessionManager::switchTo(TermSession& session)
{
    if (activeSession != &session)
    {
        previousSession = activeSession;
        logUsage("session.switch", {{"name", session.name}, {"sessions", sessions.size()}});
    }

    activeSession = &session;
    mru.touch(session.key());
    paletteUsage.recordUse(session.key(), PaletteUsageStore::seenWeight, MruStore::nowMs());
    persist();
    onActiveChanged(session);
}

PaletteUsage SessionManager::getPaletteUsage(const std::string& query)
{
    auto nowMs = MruStore::nowMs();
    auto usage = [this, nowMs](const std::string& key) { return paletteUsage.getUsage(key, nowMs); };
    return {usage, paletteUsage.findPicksForQuery(query, nowMs)};
}

void SessionManager::recordPalettePick(const std::string& query, const std::string& key)
{
    paletteUsage.recordPalettePick(query, key, MruStore::nowMs());
}

void SessionManager::switchToIndex(int index)
{
    if (index >= 0 && index < (int) sessions.size())
        switchTo(*sessions[(std::size_t) index]);
}

void SessionManager::switchToLast()
{
    if (previousSession != nullptr)
        for (auto& session: sessions)
            if (session.get() == previousSession)
            {
                switchTo(*previousSession);
                return;
            }
}

void SessionManager::beginPeek()
{
    peekOrigin = activeSession;
    peeking = true;
}

void SessionManager::peekTo(TermSession& session)
{
    if (!peeking)
        beginPeek();

    if (activeSession == &session)
        return;

    // Swap and repaint only — no mru.touch, no persist. Peeking through five
    // sessions must not stamp all five as recently used, and it must not hit
    // disk on every keystroke.
    activeSession = &session;
    onActiveChanged(session);
}

void SessionManager::endPeek(bool commit)
{
    if (!peeking)
        return;

    peeking = false;
    auto* origin = peekOrigin;
    peekOrigin = nullptr;

    if (commit)
    {
        // activeSession is already the peeked target and its view is attached;
        // now record it as a real switch away from where we started.
        if (activeSession != nullptr && activeSession != origin)
        {
            logUsage("session.switch",
                     {{"name", activeSession->name}, {"sessions", sessions.size()}});
            previousSession = origin;
            mru.touch(activeSession->key());
            persist();
        }

        return;
    }

    // Cancel: restore the session we were on before peeking.
    if (origin != nullptr && activeSession != origin)
    {
        activeSession = origin;
        onActiveChanged(*origin);
    }
}

void SessionManager::closeIfPresent(TermSession* session)
{
    for (auto& candidate: sessions)
        if (candidate.get() == session)
        {
            close(*candidate);
            return;
        }
}

void SessionManager::close(TermSession& session)
{
    const auto index = std::find_if(sessions.begin(),
                                    sessions.end(),
                                    [&](auto& s) { return s.get() == &session; });

    if (index == sessions.end())
        return;

    // An explicit session close kills its shells; app teardown, which
    // never comes through here, only detaches so they survive in the
    // daemon.
    logUsage("session.close", {{"name", session.name}, {"sessions", sessions.size() - 1}});
    session.view.terminateAll();

    auto closing = std::move(*index);
    sessions.erase(index);

    if (previousSession == closing.get())
        previousSession = nullptr;

    if (peekOrigin == closing.get())
        peekOrigin = nullptr;

    if (sessions.empty())
    {
        activeSession = nullptr;
        persist();
        onAllClosed();
        return;
    }

    if (activeSession == closing.get())
    {
        auto* next =
            previousSession != nullptr ? previousSession : sessions.back().get();
        previousSession = nullptr;
        activeSession = next;
        onActiveChanged(*next);
    }

    persist();
    onSessionsChanged();
}

void SessionManager::restoreOrCreateInitial()
{
    const auto& state = saved.peek();

    for (const auto& savedSession: state.sessions)
        createSession(savedSession.name,
                      savedSession.projectDir,
                      savedSession.cwd,
                      savedSession.panes);

    auto panes = 0;

    for (const auto& session: sessions)
        panes += session->view.paneCount();

    logUsage("session.restore", {{"sessions", sessions.size()}, {"panes", panes}});

    if (sessions.empty())
        createSession("home", eacp::FilePath::homeDirectory().str(), {}, {});

    const auto index = std::clamp(state.activeIndex, 0, (int) sessions.size() - 1);
    switchTo(*sessions[(std::size_t) index]);
}

SessionManager::~SessionManager()
{
    *alive = false;

    if (persistPending)
        writeState();
}

void SessionManager::persist()
{
    if (persistPending)
        return;

    persistPending = true;

    eacp::Threads::delay(persistDelay)
        .then(
            [this, guard = std::weak_ptr<bool> {alive}]
            {
                if (guard.expired() || !persistPending)
                    return;

                persistPending = false;
                writeState();
            });
}

void SessionManager::persistNow()
{
    persistPending = false;
    writeState();
}

void SessionManager::writeState()
{
    saved.mutate(
        [&](SavedState& state)
        {
            state.sessions.clear();

            for (auto& session: sessions)
                state.sessions.push_back({session->name,
                                          session->projectDir,
                                          session->activeWorkingDirectory(),
                                          session->view.snapshot()});

            state.activeIndex = 0;

            for (std::size_t i = 0; i < sessions.size(); ++i)
                if (sessions[i].get() == activeSession)
                    state.activeIndex = (int) i;
        });

    publishLayouts(saved.peek());
}

// The daemon of each session's machine keeps its layout, so another Mac or
// the phone attaching there knows which panes form the session.
void SessionManager::publishLayouts(const SavedState& state) const
{
    for (const auto& session: state.sessions)
    {
        const auto host = getProjectHost(session.projectDir);

        if (auto* daemon = findDaemonOfHost(host))
            daemon->send(makeLayoutMessage(session));
    }
}
} // namespace term
