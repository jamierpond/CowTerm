#include "AgentBoard.h"

#include <cerrno>
#include <csignal>

#include <Background.h>
#include <Sessions.h>

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace term
{
namespace
{
constexpr auto cardKeySeparator = std::string_view {"@"};
constexpr auto unknownBranchName = std::string_view {"no branch"};

struct ActivityDescription
{
    SessionActivity activity;
    std::string_view name;
    int urgency;
};

constexpr auto activityDescriptions = std::array {
    ActivityDescription {SessionActivity::needsYou, "needs you", 3},
    ActivityDescription {SessionActivity::busy, "busy", 2},
    ActivityDescription {SessionActivity::idle, "idle", 1},
};

const ActivityDescription& describeActivity(SessionActivity activity)
{
    return *std::ranges::find(activityDescriptions, activity, &ActivityDescription::activity);
}

struct SessionState
{
    std::string sessionId;
    std::string transcriptPath;
    std::string workingDirectory;
    std::string gitBranch;
    std::string title;
    std::string statusText;
    std::string lastTimestamp;
    std::string paneShellId;
    bool hasUserMessage = false;
    SessionActivity activity = SessionActivity::idle;
};

SessionActivity chooseActivityForAgent(const claudecli::AgentEntry& agent)
{
    auto status = agent.status.value_or("");
    auto state = agent.state.value_or("");

    if (status == "waiting" || state == "blocked")
        return SessionActivity::needsYou;

    if (status == "busy" || state == "working")
        return SessionActivity::busy;

    return SessionActivity::idle;
}

std::string makeTranscriptPath(const BoardQuery& query,
                               const std::string& workingDirectory,
                               const std::string& sessionId)
{
    auto directory =
        claudecli::getProjectTranscriptDirectory(query.configDirectory, workingDirectory);
    return directory + "/" + sessionId + ".jsonl";
}

std::string describeAgentStatus(const claudecli::AgentEntry& agent)
{
    if (agent.waitingFor)
        return "waiting: " + *agent.waitingFor;

    if (agent.status)
        return *agent.status;

    if (agent.state)
        return *agent.state;

    return "idle";
}

std::string chooseSessionTitle(const claudecli::SessionSummary& summary)
{
    if (!summary.title.empty())
        return summary.title;

    return eacp::Strings::trim(summary.firstPrompt);
}

std::optional<claudecli::SessionSummary>
    readSessionSummary(const BoardQuery& query,
                       const std::string& workingDirectory,
                       const std::string& sessionId)
{
    auto transcript =
        claudecli::readTranscript(makeTranscriptPath(query, workingDirectory, sessionId));

    if (transcript.records.empty())
        return std::nullopt;

    return claudecli::summariseTranscript(transcript);
}

} // namespace

bool isProcessIdRunning(std::int64_t processId)
{
    auto nativeProcessId = static_cast<pid_t>(processId);
    return kill(nativeProcessId, 0) == 0 || errno == EPERM;
}

namespace
{
SessionState makeLiveSessionState(const BoardQuery& query,
                                  const claudecli::AgentEntry& agent,
                                  const std::string& paneShellId)
{
    auto session = SessionState {};
    session.sessionId = agent.sessionId;
    session.transcriptPath = makeTranscriptPath(query, agent.cwd, agent.sessionId);
    session.workingDirectory = agent.cwd;
    session.activity = chooseActivityForAgent(agent);
    session.paneShellId = paneShellId;
    session.statusText = describeAgentStatus(agent);
    session.title = agent.name.value_or("");

    if (auto summary = readSessionSummary(query, agent.cwd, agent.sessionId))
    {
        session.gitBranch = summary->gitBranch;
        session.lastTimestamp = summary->lastTimestamp;
        session.hasUserMessage = !eacp::Strings::trim(summary->firstPrompt).empty();

        if (auto title = chooseSessionTitle(*summary); !title.empty())
            session.title = title;
    }

    return session;
}

std::string getLastPathComponent(const std::string& path)
{
    auto separator = path.find_last_of('/');

    if (separator == std::string::npos)
        return path;

    return path.substr(separator + 1);
}

std::string makeTaskName(const SessionState& session)
{
    auto branch = session.gitBranch.empty() ? std::string {unknownBranchName}
                                            : session.gitBranch;
    return getLastPathComponent(session.workingDirectory) + " · " + branch;
}

std::string makeBadgeText(const std::vector<SessionState>& sessions,
                          const SessionState& mostUrgent)
{
    if (sessions.size() == 1)
        return mostUrgent.statusText;

    return eacp::Strings::concat(mostUrgent.statusText, " · ", sessions.size(), " agents");
}

bool isMoreUrgentOrNewer(const SessionState& candidate, const SessionState& current)
{
    auto candidateUrgency = describeActivity(candidate.activity).urgency;
    auto currentUrgency = describeActivity(current.activity).urgency;

    if (candidateUrgency != currentUrgency)
        return candidateUrgency > currentUrgency;

    return candidate.lastTimestamp > current.lastTimestamp;
}

Zone chooseDefaultZone(SessionActivity activity)
{
    if (activity == SessionActivity::needsYou)
        return Zone::needsYou;

    return Zone::working;
}

TaskCard makeTaskCard(const std::vector<SessionState>& sessions)
{
    auto mostUrgent = sessions.front();

    for (const auto& session: sessions)
    {
        if (isMoreUrgentOrNewer(session, mostUrgent))
            mostUrgent = session;
    }

    const auto& newest = std::ranges::max(sessions, {}, &SessionState::lastTimestamp);

    auto card = TaskCard {};
    card.key = makeCardKey(mostUrgent.workingDirectory, mostUrgent.gitBranch);
    card.taskName = makeTaskName(mostUrgent);
    card.workingDirectory = mostUrgent.workingDirectory;
    card.gitBranch = mostUrgent.gitBranch;
    card.activity = mostUrgent.activity;
    card.zone = chooseDefaultZone(mostUrgent.activity);
    card.sessionCount = static_cast<int>(sessions.size());
    card.badgeText = makeBadgeText(sessions, mostUrgent);
    card.description = mostUrgent.title;
    card.latestActivityTimestamp = newest.lastTimestamp;
    card.paneShellId = mostUrgent.paneShellId;

    for (const auto& session: sessions)
        card.sessionIds.push_back(session.sessionId);
    return card;
}

bool isPaneKnown(const BoardQuery& query, const std::string& shellId)
{
    return std::ranges::any_of(query.panes,
                               [&shellId](const BoardPane& pane)
                               { return pane.shellId == shellId; });
}

// The pane the agent's process inherited COWTERM_PANE from; failing that (claude
// started under tmux in a pane, say), an unclaimed claude pane in the same directory.
std::optional<std::string> findPaneForAgent(const BoardQuery& query,
                                            const claudecli::AgentEntry& agent,
                                            const std::set<std::string>& claimedPaneShellIds)
{
    if (query.findPaneOfProcess)
    {
        auto inherited = query.findPaneOfProcess(*agent.pid);

        if (inherited && isPaneKnown(query, *inherited))
            return inherited;
    }

    for (const auto& pane: query.panes)
    {
        auto isCandidate = pane.isRunningClaude && pane.workingDirectory == agent.cwd
                           && !claimedPaneShellIds.contains(pane.shellId);

        if (isCandidate)
            return pane.shellId;
    }

    return std::nullopt;
}
} // namespace

std::string makeCardKey(std::string_view workingDirectory, std::string_view gitBranch)
{
    return eacp::Strings::concat(workingDirectory, cardKeySeparator, gitBranch);
}

std::string_view getSessionActivityName(SessionActivity activity)
{
    return describeActivity(activity).name;
}

BoardSnapshot collectBoardSnapshot(const BoardQuery& query)
{
    auto command = claudecli::AgentsCommand {};
    command.all = true;
    auto agentList = claudecli::listAgents(query.launch, command);

    if (!agentList.commandOutput.hasLaunched)
    {
        auto snapshot = BoardSnapshot {};
        snapshot.problem = "claude did not start";
        return snapshot;
    }

    return buildBoardSnapshot(query, agentList.listing);
}

BoardSnapshot buildBoardSnapshot(const BoardQuery& query,
                                 const claudecli::AgentListing& listing)
{
    auto snapshot = BoardSnapshot {};

    if (!listing.parseError.empty())
        snapshot.problem = "claude agents: " + listing.parseError;

    auto sessions = std::vector<SessionState> {};
    auto claimedPaneShellIds = std::set<std::string> {};

    for (const auto& agent: listing.agents)
    {
        if (!agent.pid || !query.isProcessRunning(*agent.pid))
            continue;

        auto paneShellId = findPaneForAgent(query, agent, claimedPaneShellIds);

        if (!paneShellId)
            continue;

        auto session = makeLiveSessionState(query, agent, *paneShellId);

        if (!session.hasUserMessage)
            continue;

        claimedPaneShellIds.insert(*paneShellId);
        sessions.push_back(std::move(session));
    }

    auto sessionsByTask = std::map<std::pair<std::string, std::string>, std::vector<SessionState>> {};

    for (const auto& session: sessions)
        sessionsByTask[{session.workingDirectory, session.gitBranch}].push_back(session);

    for (const auto& entry: sessionsByTask)
        snapshot.cards.push_back(makeTaskCard(entry.second));

    std::ranges::stable_sort(snapshot.cards,
                             std::ranges::greater {},
                             &TaskCard::latestActivityTimestamp);

    return snapshot;
}

void applyBoardDocument(BoardSnapshot& snapshot, const BoardDocument& document)
{
    for (auto& card: snapshot.cards)
    {
        auto storedZone = getStoredZone(document, card.key);
        auto isStoredZoneShown = storedZone && card.activity != SessionActivity::needsYou;

        if (isStoredZoneShown)
            card.zone = *storedZone;
    }
}

std::optional<TaskCard> findCardOfSession(const BoardSnapshot& snapshot, const std::string& sessionId)
{
    for (const auto& card: snapshot.cards)
    {
        if (std::ranges::find(card.sessionIds, sessionId) != card.sessionIds.end())
            return card;
    }

    return std::nullopt;
}

std::vector<TaskCard> getCardsInZone(const BoardSnapshot& snapshot, Zone zone)
{
    auto cards = std::vector<TaskCard> {};

    for (const auto& card: snapshot.cards)
    {
        if (card.zone == zone)
            cards.push_back(card);
    }

    std::ranges::stable_partition(
        cards, [](const TaskCard& card) { return card.activity == SessionActivity::needsYou; });
    return cards;
}

} // namespace term
