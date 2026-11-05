#include "TaskBoard.h"

#include "CardText.h"
#include "ProcessEnvironment.h"
#include "Protocol.h"
#include "TextWrapping.h"

#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <thread>

namespace term
{
using namespace eacp;
using UI::Color;
using UI::KeyEvent;
using UI::Rect;
namespace KeyCode = UI::KeyCode;

namespace
{
constexpr auto refreshIntervalMilliseconds = 2000;
constexpr auto outerMargin = 16.0f;
constexpr auto headerHeight = 34.0f;
constexpr auto footerHeight = 30.0f;
constexpr auto headerTextTop = 8.0f;
constexpr auto footerTextTop = 6.0f;
constexpr auto descriptionAlpha = 0.8f;
constexpr auto columnGap = 12.0f;
constexpr auto columnPadding = 10.0f;
constexpr auto columnHeaderHeight = 30.0f;
constexpr auto columnCornerRadius = 8.0f;
constexpr auto minimumColumnWidth = 200.0f;
constexpr auto cardGap = 8.0f;
constexpr auto cardPadding = 10.0f;
constexpr auto cardCornerRadius = 6.0f;
constexpr auto cardLineHeight = 18.0f;
constexpr auto cardTextBaselineOffset = 13.0f;
constexpr auto ageGap = 8.0f;
constexpr auto taskNameLineCount = 2;
constexpr auto descriptionLineCount = 2;
constexpr auto badgeLineCount = 1;
constexpr auto sectionGap = 14.0f;
constexpr auto backdropAlpha = 0.97f;
constexpr auto columnBrightening = 0.03f;
constexpr auto cardBrightening = 0.07f;
constexpr auto selectedBorderWidth = 1.5f;
constexpr auto cardBorderWidth = 1.0f;
constexpr auto dragHighlightAlpha = 0.12f;
constexpr auto dragStartDistance = 4.0f;
constexpr auto headerFontSize = 14.0f;
constexpr auto titleFontSize = 13.0f;
constexpr auto detailFontSize = 12.0f;
constexpr auto secondsPerMinute = std::int64_t {60};
constexpr auto secondsPerHour = std::int64_t {3600};
constexpr auto secondsPerDay = std::int64_t {86400};
constexpr auto dimColourIndex = std::size_t {8};
constexpr auto separator = std::string_view {" · "};

struct ZoneColourIndex
{
    Zone zone;
    std::size_t ansiIndex;
};

constexpr auto zoneColourIndices = std::array {
    ZoneColourIndex {Zone::needsYou, 1},
    ZoneColourIndex {Zone::working, 3},
    ZoneColourIndex {Zone::review, 6},
    ZoneColourIndex {Zone::done, 2},
};

struct ZoneCountLabel
{
    Zone zone;
    std::string_view label;
};

constexpr auto zoneCountLabels = std::array {
    ZoneCountLabel {Zone::needsYou, "need you"},
    ZoneCountLabel {Zone::working, "working"},
    ZoneCountLabel {Zone::review, "to review"},
};

std::optional<std::int64_t> parseUtcTimestampSeconds(const std::string& timestamp)
{
    auto parts = std::tm {};

    if (strptime(timestamp.c_str(), "%Y-%m-%dT%H:%M:%S", &parts) == nullptr)
        return std::nullopt;

    return (std::int64_t) timegm(&parts);
}

std::string formatAge(const std::string& timestamp)
{
    auto seconds = parseUtcTimestampSeconds(timestamp);

    if (!seconds)
        return {};

    auto age = std::max<std::int64_t>((std::int64_t) std::time(nullptr) - *seconds, 0);

    if (age < secondsPerMinute)
        return std::to_string(age) + "s";

    if (age < secondsPerHour)
        return std::to_string(age / secondsPerMinute) + "m";

    if (age < secondsPerDay)
        return std::to_string(age / secondsPerHour) + "h";

    return std::to_string(age / secondsPerDay) + "d";
}

void drawTextLine(UI::Graphics& g, const std::string& text, float x, float top)
{
    g.drawText(text, {x, top + cardTextBaselineOffset});
}
} // namespace

TaskBoard::TaskBoard(const AppConfig& configToUse)
    : config(configToUse)
    , theme(themeByName(configToUse.theme))
    , headerFont({config.font, headerFontSize})
    , titleFont({config.font, titleFontSize})
    , detailFont({config.font, detailFontSize})
{
    setInterceptsMouseClicks(true);
    setWantsKeyboardFocus(true);
    columns.resize(getColumnZonesInDisplayOrder().size());
}

void TaskBoard::setClaudeQuery(const BoardQuery& query)
{
    claudeQuery = query;
}

void TaskBoard::setStoreDirectory(const FilePath& directory)
{
    boardDatabase.emplace(openBoardDatabase(directory));
    boardDocument.emplace(openBoardDocument(*boardDatabase));
}

void TaskBoard::setStartAgentSettings(const StartAgentSettings& settings)
{
    startAgentSettings = settings;
}

void TaskBoard::show()
{
    shown = true;
    isLeaderArmed = false;
    isMoveArmed = false;
    mode = Mode::board;

    if (!claudeQuery)
    {
        hasLoaded = true;
        snapshot.problem = "claude not found on PATH or in ~/.local/bin";
    }

    requestRefresh();
    refreshTimer = std::make_unique<Threads::Timer>([this] { requestRefresh(); },
                                                    refreshIntervalMilliseconds);
    repaint();
}

void TaskBoard::showSnapshot(const BoardSnapshot& snapshotToShow)
{
    shown = true;
    hasLoaded = true;
    applySnapshot(snapshotToShow);
}

void TaskBoard::selectCard(const std::string& key)
{
    selectedKey = key;
    clampSelection();
    repaint();
}

void TaskBoard::requestRefresh()
{
    if (!claudeQuery || isRefreshing)
        return;

    isRefreshing = true;
    auto query = *claudeQuery;
    query.panes = getPanes();
    query.findPaneOfProcess = [](std::int64_t processId)
    { return readProcessEnvironmentVariable(processId, proto::paneEnvironmentVariable); };

    std::thread(
        [query = std::move(query), guard = std::weak_ptr<bool> {alive}, this]
        {
            auto collected = collectBoardSnapshot(query);

            Threads::callAsync(
                [collected = std::move(collected), guard, this]() mutable
                {
                    if (guard.expired())
                        return;

                    isRefreshing = false;
                    hasLoaded = true;

                    if (shown)
                        applySnapshot(std::move(collected));
                });
        })
        .detach();
}

void TaskBoard::applySnapshot(BoardSnapshot newSnapshot)
{
    snapshot = std::move(newSnapshot);

    if (boardDocument)
        applyBoardDocument(snapshot, boardDocument->get());

    const auto& zones = getColumnZonesInDisplayOrder();

    for (std::size_t index = 0; index < zones.size(); ++index)
        columns[index] = getCardsInZone(snapshot, zones[index]);

    clampSelection();
    repaint();
}

// Keeps the highlight on the same card when cards move between refreshes.
void TaskBoard::clampSelection()
{
    for (std::size_t column = 0; column < columns.size(); ++column)
    {
        auto found = std::ranges::find(columns[column], selectedKey, &TaskCard::key);

        if (found != columns[column].end())
        {
            selectedColumn = column;
            selectedRow = (std::size_t) std::distance(columns[column].begin(), found);
            return;
        }
    }

    if (columns[selectedColumn].empty())
    {
        auto firstFilled = std::ranges::find_if(
            columns, [](const auto& cards) { return !cards.empty(); });

        if (firstFilled != columns.end())
            selectedColumn = (std::size_t) std::distance(columns.begin(), firstFilled);
    }

    const auto& cards = columns[selectedColumn];
    selectedRow = cards.empty() ? 0 : std::min(selectedRow, cards.size() - 1);

    if (!cards.empty())
        selectedKey = cards[selectedRow].key;
}

std::optional<TaskCard> TaskBoard::getSelectedCard() const
{
    const auto& cards = columns[selectedColumn];

    if (selectedRow >= cards.size())
        return std::nullopt;

    return cards[selectedRow];
}

void TaskBoard::moveSelectionWithinColumn(int delta)
{
    const auto& cards = columns[selectedColumn];

    if (cards.empty())
        return;

    const auto count = (int) cards.size();
    selectedRow = (std::size_t) std::clamp((int) selectedRow + delta, 0, count - 1);
    selectedKey = cards[selectedRow].key;
    repaint();
}

// Skips empty columns: there is nothing to land on in them.
void TaskBoard::moveSelectionAcrossColumns(int delta)
{
    auto column = (int) selectedColumn;
    const auto count = (int) columns.size();

    for (column += delta; column >= 0 && column < count; column += delta)
    {
        const auto& cards = columns[(std::size_t) column];

        if (cards.empty())
            continue;

        selectedColumn = (std::size_t) column;
        selectedRow = std::min(selectedRow, cards.size() - 1);
        selectedKey = cards[selectedRow].key;
        repaint();
        return;
    }
}

void TaskBoard::focusSelectedCard()
{
    if (!shown)
        return;

    auto card = getSelectedCard();

    if (!card)
        return;

    close();
    onShowPane(card->paneShellId);
}

void TaskBoard::moveSelectedCard(Zone zone)
{
    auto card = getSelectedCard();

    if (!card || !boardDocument)
        return;

    auto document = boardDocument->get();

    if (moveCard(document, card->key, zone, MoveRequester::jamie) != MoveOutcome::moved)
        return;

    boardDocument->set(document);
    onTellPane(card->paneShellId, getZoneMessageForAgent(zone));
    applySnapshot(snapshot);
}

void TaskBoard::close()
{
    shown = false;
    refreshTimer.reset();
    onClosed();
}

bool TaskBoard::keyDown(const KeyEvent& event)
{
    const auto& characters = event.charactersIgnoringModifiers;

    if (mode == Mode::pickProject)
        return pickerKeyDown(event);

    if (mode == Mode::nameTask)
        return nameTaskKeyDown(event);

    if (mode == Mode::pullRequests)
        return pullRequestKeyDown(event);

    if (mode == Mode::startingAgent)
    {
        if (event.keyCode == KeyCode::Escape && !startProblem.empty())
        {
            mode = Mode::nameTask;
            repaint();
        }

        return true;
    }

    if (characters == "p" && !event.modifiers.control)
    {
        beginShowingPullRequests();
        return true;
    }

    if (characters == "n" && !event.modifiers.control)
    {
        beginPickingProject();
        return true;
    }

    if (std::exchange(isLeaderArmed, false))
    {
        if (characters == "b")
            close();

        return true;
    }

    if (std::exchange(isMoveArmed, false))
    {
        if (auto zone = findZoneByMoveLetter(characters.empty() ? '\0' : characters.front()))
            moveSelectedCard(*zone);

        repaint();
        return true;
    }

    if (characters == "m")
    {
        isMoveArmed = true;
        repaint();
        return true;
    }

    if (event.modifiers.control && characters == "a")
    {
        isLeaderArmed = true;
        return true;
    }

    if (event.keyCode == KeyCode::Escape || characters == "q")
    {
        close();
        return true;
    }

    if (event.keyCode == KeyCode::Return)
    {
        focusSelectedCard();
        return true;
    }

    if (event.keyCode == KeyCode::DownArrow || characters == "j")
        moveSelectionWithinColumn(1);
    else if (event.keyCode == KeyCode::UpArrow || characters == "k")
        moveSelectionWithinColumn(-1);
    else if (event.keyCode == KeyCode::LeftArrow || characters == "h")
        moveSelectionAcrossColumns(-1);
    else if (event.keyCode == KeyCode::RightArrow || characters == "l")
        moveSelectionAcrossColumns(1);

    return true;
}

bool TaskBoard::isNarrowLayout() const
{
    const auto columnCount = (float) columns.size();
    const auto needed = columnCount * minimumColumnWidth
                        + (columnCount - 1.0f) * columnGap + 2.0f * outerMargin;
    return getWidth() < needed;
}

Color TaskBoard::getZoneColour(Zone zone) const
{
    auto found = std::ranges::find(zoneColourIndices, zone, &ZoneColourIndex::zone);
    return toColor(theme.ansi[found->ansiIndex]);
}

namespace
{
bool isTypedText(const KeyEvent& event)
{
    const auto& text = event.characters;
    auto isControlCharacter = text.empty() || (unsigned char) text[0] < 0x20 || text[0] == 0x7f;
    return !isControlCharacter && !event.modifiers.command && !event.modifiers.control;
}

void removeLastCharacter(std::string& text)
{
    constexpr auto utf8ContinuationMask = 0xC0u;
    constexpr auto utf8ContinuationPattern = 0x80u;

    while (!text.empty() && ((unsigned char) text.back() & utf8ContinuationMask) == utf8ContinuationPattern)
        text.pop_back();

    if (!text.empty())
        text.pop_back();
}
} // namespace

void TaskBoard::showProjectPicker(const std::vector<Project>& projectsToShow,
                                  const std::string& query)
{
    projects = projectsToShow;
    hasRequestedProjects = true;
    mode = Mode::pickProject;
    pickerQuery = query;
    applyPickerQuery();
    repaint();
}

void TaskBoard::beginPickingProject()
{
    mode = Mode::pickProject;
    pickerQuery.clear();
    selectedProject = 0;
    startProblem.clear();
    loadProjects();
    applyPickerQuery();
    repaint();
}

// Local checkouts at once; gh's repositories join them when gh answers.
void TaskBoard::loadProjects()
{
    if (!startAgentSettings || hasRequestedProjects)
        return;

    hasRequestedProjects = true;
    projects = listLocalProjects(startAgentSettings->projectsDirectory);
    auto tools = GitHubTools {startAgentSettings->ghExecutable};

    std::thread(
        [tools, guard = std::weak_ptr<bool> {alive}, this]
        {
            auto remote = listRemoteProjects(tools);

            Threads::callAsync(
                [remote = std::move(remote), guard, this]
                {
                    if (guard.expired())
                        return;

                    projects = mergeProjects(std::move(projects), remote);
                    applyPickerQuery();
                    repaint();
                });
        })
        .detach();
}

void TaskBoard::applyPickerQuery()
{
    shownProjects = filterProjects(projects, pickerQuery);
    selectedProject = shownProjects.empty() ? 0 : std::min(selectedProject, shownProjects.size() - 1);
}

bool TaskBoard::pickerKeyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        mode = Mode::board;
    }
    else if (event.keyCode == KeyCode::Return)
    {
        chooseSelectedProject();
    }
    else if (event.keyCode == KeyCode::UpArrow
             || (event.modifiers.control && event.charactersIgnoringModifiers == "p"))
    {
        selectedProject = selectedProject == 0 ? 0 : selectedProject - 1;
    }
    else if (event.keyCode == KeyCode::DownArrow
             || (event.modifiers.control && event.charactersIgnoringModifiers == "n"))
    {
        if (selectedProject + 1 < shownProjects.size())
            ++selectedProject;
    }
    else if (event.keyCode == KeyCode::Delete)
    {
        removeLastCharacter(pickerQuery);
        applyPickerQuery();
    }
    else if (isTypedText(event))
    {
        pickerQuery += event.characters;
        selectedProject = 0;
        applyPickerQuery();
    }

    repaint();
    return true;
}

void TaskBoard::chooseSelectedProject()
{
    if (selectedProject >= shownProjects.size())
        return;

    chosenProject = shownProjects[selectedProject];
    taskTitle.clear();
    mode = Mode::nameTask;
}

bool TaskBoard::nameTaskKeyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
        mode = Mode::pickProject;
    else if (event.keyCode == KeyCode::Return && !taskTitle.empty())
        startAgentInWorktree();
    else if (event.keyCode == KeyCode::Delete)
        removeLastCharacter(taskTitle);
    else if (isTypedText(event))
        taskTitle += event.characters;

    repaint();
    return true;
}

void TaskBoard::startAgentInWorktree()
{
    if (!startAgentSettings || !chosenProject)
        return;

    mode = Mode::startingAgent;
    startProblem.clear();
    startProgress = "starting";

    auto request = WorkspaceRequest {};
    request.project = *chosenProject;
    request.projectsDirectory = startAgentSettings->projectsDirectory;
    request.taskBranch = makeTaskBranchName(taskTitle);
    request.gitExecutable = startAgentSettings->gitExecutable;
    request.ghExecutable = startAgentSettings->ghExecutable;

    std::thread(
        [request, guard = std::weak_ptr<bool> {alive}, this]
        {
            auto reportProgress = [guard, this](const std::string& progress)
            {
                Threads::callAsync(
                    [progress, guard, this]
                    {
                        if (guard.expired())
                            return;

                        startProgress = progress;
                        repaint();
                    });
            };

            auto prepared = prepareWorkspace(request, reportProgress);

            Threads::callAsync(
                [prepared, guard, this]
                {
                    if (guard.expired())
                        return;

                    if (!prepared.hasSucceeded)
                    {
                        startProblem = prepared.problem;
                        repaint();
                        return;
                    }

                    mode = Mode::board;
                    close();
                    onStartAgent(prepared.worktreePath);
                });
        })
        .detach();
}

void TaskBoard::paintPicker(UI::Graphics& g)
{
    constexpr auto pickerWidth = 640.0f;
    constexpr auto pickerTopFraction = 0.12f;
    constexpr auto pickerRowCount = std::size_t {12};
    constexpr auto pickerRowHeight = 24.0f;
    constexpr auto pickerPadding = 14.0f;
    constexpr auto pickerCornerRadius = 10.0f;
    constexpr auto pickerHeaderLineCount = 2.0f;
    constexpr auto panelBrightening = 0.05f;

    const auto bounds = getLocalBounds();
    const auto width = std::min(pickerWidth, bounds.w - 2.0f * outerMargin);
    const auto height = 2.0f * pickerPadding
                        + (pickerHeaderLineCount + (float) pickerRowCount) * pickerRowHeight;
    const auto panel = Rect {(bounds.w - width) / 2.0f,
                             std::max(bounds.h * pickerTopFraction, outerMargin),
                             width,
                             std::min(height, bounds.h - 2.0f * outerMargin)};

    g.setColour(toColor(theme.background).brighter(panelBrightening));
    g.fillRoundedRect(panel, pickerCornerRadius);
    g.setColour(toColor(theme.selection));
    g.drawRect(panel, cardBorderWidth);

    const auto left = panel.x + pickerPadding;
    const auto textWidth = panel.w - 2.0f * pickerPadding;
    auto top = panel.y + pickerPadding;
    auto measure = [&g](std::string_view text) { return g.measureText(text); };

    auto drawFittedLine = [&](const std::string& text, Color colour)
    {
        auto lines = wrapTextToWidth(text, textWidth, 1, measure);
        g.setColour(colour);
        drawTextLine(g, lines.empty() ? std::string {} : lines.front(), left, top);
        top += pickerRowHeight;
    };

    g.setFont(titleFont);
    const auto dim = toColor(theme.ansi[dimColourIndex]);

    if (mode == Mode::pickProject)
    {
        drawFittedLine("new agent in: " + pickerQuery + "▏", toColor(theme.foreground));
        drawFittedLine("type to filter · enter pick · esc back", dim);
        g.setFont(detailFont);

        for (std::size_t row = 0; row < shownProjects.size() && row < pickerRowCount; ++row)
        {
            if (top + pickerRowHeight > panel.bottom())
                break;

            const auto isSelected = row == selectedProject;

            if (isSelected)
            {
                g.setColour(toColor(theme.selection));
                g.fillRoundedRect({panel.x + pickerPadding / 2.0f, top, panel.w - pickerPadding, pickerRowHeight},
                                  cardCornerRadius);
            }

            placedPickerRows.push_back({panel.x, top, panel.w, pickerRowHeight});
            drawFittedLine(describeProject(shownProjects[row]), toColor(theme.foreground));
        }

        return;
    }

    const auto projectName = chosenProject ? chosenProject->name : std::string {};
    drawFittedLine(projectName + ": " + taskTitle + "▏", toColor(theme.foreground));
    g.setFont(detailFont);
    drawFittedLine("branch " + makeTaskBranchName(taskTitle.empty() ? std::string {"…"} : taskTitle), dim);

    if (mode == Mode::nameTask)
    {
        drawFittedLine("type the task's title · enter start claude · esc back", dim);
        return;
    }

    if (!startProblem.empty())
    {
        auto lines = wrapTextToWidth(startProblem, textWidth, (int) pickerRowCount, measure);
        g.setColour(getZoneColour(Zone::needsYou));

        for (const auto& line: lines)
        {
            drawTextLine(g, line, left, top);
            top += pickerRowHeight;
        }

        drawFittedLine("esc back", dim);
        return;
    }

    drawFittedLine(startProgress + "…", getZoneColour(Zone::working));
}

void TaskBoard::paint(UI::Graphics& g)
{
    placedCards.clear();
    placedColumns.clear();
    placedPickerRows.clear();
    placedPullRequestRows.clear();

    const auto bounds = getLocalBounds();
    g.setColour(toColor(theme.background, backdropAlpha));
    g.fillRect(bounds);

    const auto contentWidth = bounds.w - 2.0f * outerMargin;
    const auto header = Rect {outerMargin, 0.0f, contentWidth, headerHeight};
    const auto footer =
        Rect {outerMargin, bounds.h - footerHeight, contentWidth, footerHeight};
    const auto body = Rect {outerMargin,
                            headerHeight,
                            contentWidth,
                            bounds.h - headerHeight - footerHeight};

    // The picker replaces the board: text draws above every fill, so cards
    // under a panel would show through it.
    if (mode == Mode::pullRequests)
    {
        paintPullRequests(g);
        return;
    }

    if (mode != Mode::board)
    {
        paintPicker(g);
        return;
    }

    paintHeader(g, header);

    if (isNarrowLayout())
        paintStacked(g, body);
    else
        paintColumns(g, body);

    paintFooter(g, footer);
}

void TaskBoard::paintHeader(UI::Graphics& g, Rect area)
{
    auto summary = std::string {"board"};

    for (const auto& countLabel: zoneCountLabels)
    {
        auto count = std::ranges::count(snapshot.cards, countLabel.zone, &TaskCard::zone);

        if (count > 0)
            summary += std::string {separator} + std::to_string(count) + " "
                       + std::string {countLabel.label};
    }

    if (!hasLoaded)
        summary += std::string {separator} + "asking claude…";
    else if (!snapshot.problem.empty())
        summary += std::string {separator} + snapshot.problem;
    else if (snapshot.cards.empty())
        summary += std::string {separator} + "no claude in any pane";

    g.setFont(headerFont);
    g.setColour(toColor(theme.foreground));
    auto lines = wrapTextToWidth(
        summary, area.w, 1, [&g](std::string_view text) { return g.measureText(text); });
    drawTextLine(g, lines.empty() ? std::string {} : lines.front(), area.x, area.y + headerTextTop);
}

void TaskBoard::paintFooter(UI::Graphics& g, Rect area)
{
    g.setFont(detailFont);
    g.setColour(toColor(theme.ansi[dimColourIndex]));
    auto hint = isMoveArmed ? "move to: w working · r review · d done · any other key cancels"
                            : "j/k card · h/l column · enter go to pane · m move · n new agent · p pull requests · esc close";
    auto lines = wrapTextToWidth(hint,
                                 area.w,
                                 1,
                                 [&g](std::string_view text) { return g.measureText(text); });
    drawTextLine(g, lines.empty() ? std::string {} : lines.front(), area.x, area.y + footerTextTop);
}

float TaskBoard::paintCard(
    UI::Graphics& g, const TaskCard& card, Rect area, bool isSelected, bool isDrawn)
{
    const auto textWidth = area.w - 2.0f * cardPadding;
    auto measure = [&g](std::string_view text) { return g.measureText(text); };

    g.setFont(detailFont);
    const auto age = formatAge(card.latestActivityTimestamp);
    const auto ageWidth = age.empty() ? 0.0f : g.measureText(age) + ageGap;

    g.setFont(titleFont);
    auto titleLines = wrapTextToWidth(card.taskName, textWidth - ageWidth, taskNameLineCount, measure);

    g.setFont(detailFont);
    auto descriptionLines =
        wrapTextToWidth(getShownCardDescription(card), textWidth, descriptionLineCount, measure);
    auto badgeLines = wrapTextToWidth(getShownCardBadge(card), textWidth, badgeLineCount, measure);

    const auto lineCount = titleLines.size() + descriptionLines.size() + badgeLines.size();
    const auto height = 2.0f * cardPadding + (float) lineCount * cardLineHeight;

    if (!isDrawn)
        return height;

    const auto cardBounds = Rect {area.x, area.y, area.w, height};
    g.setColour(toColor(theme.background).brighter(cardBrightening));
    g.fillRoundedRect(cardBounds, cardCornerRadius);

    if (isSelected)
    {
        g.setColour(toColor(theme.foreground));
        g.drawRect(cardBounds, selectedBorderWidth);
    }
    else
    {
        g.setColour(toColor(theme.selection));
        g.drawRect(cardBounds, cardBorderWidth);
    }

    auto top = area.y + cardPadding;
    const auto left = area.x + cardPadding;

    g.setFont(titleFont);
    g.setColour(toColor(theme.foreground));

    for (const auto& line: titleLines)
    {
        drawTextLine(g, line, left, top);
        top += cardLineHeight;
    }

    if (!age.empty())
    {
        g.setFont(detailFont);
        g.setColour(toColor(theme.ansi[dimColourIndex]));
        drawTextLine(g, age, area.right() - cardPadding - g.measureText(age), area.y + cardPadding);
    }

    g.setFont(detailFont);
    auto isAskingJamie = card.activity == SessionActivity::needsYou;
    g.setColour(isAskingJamie ? getZoneColour(Zone::needsYou)
                              : toColor(theme.foreground).withAlpha(descriptionAlpha));

    for (const auto& line: descriptionLines)
    {
        drawTextLine(g, line, left, top);
        top += cardLineHeight;
    }

    g.setColour(toColor(theme.ansi[dimColourIndex]));

    for (const auto& line: badgeLines)
    {
        drawTextLine(g, line, left, top);
        top += cardLineHeight;
    }

    return height;
}

void TaskBoard::paintColumns(UI::Graphics& g, Rect area)
{
    const auto& zones = getColumnZonesInDisplayOrder();
    const auto columnCount = (float) zones.size();
    const auto columnWidth = (area.w - (columnCount - 1.0f) * columnGap) / columnCount;

    for (std::size_t column = 0; column < zones.size(); ++column)
    {
        const auto zone = zones[column];
        const auto columnBounds = Rect {
            area.x + (float) column * (columnWidth + columnGap), area.y, columnWidth, area.h};

        g.setColour(toColor(theme.background).brighter(columnBrightening));
        g.fillRoundedRect(columnBounds, columnCornerRadius);
        placedColumns.push_back({column, columnBounds});

        if (dragTargetZone == zone)
        {
            g.setColour(getZoneColour(zone).withAlpha(dragHighlightAlpha));
            g.fillRoundedRect(columnBounds, columnCornerRadius);
            g.setColour(getZoneColour(zone));
            g.drawRect(columnBounds, selectedBorderWidth);
        }

        g.setFont(titleFont);
        g.setColour(getZoneColour(zone));
        drawTextLine(g,
                     std::string {getZoneName(zone)},
                     columnBounds.x + columnPadding,
                     columnBounds.y + columnPadding);

        const auto& cards = columns[column];
        const auto count = std::to_string(cards.size());
        g.setColour(toColor(theme.ansi[dimColourIndex]));
        drawTextLine(g,
                     count,
                     columnBounds.right() - columnPadding - g.measureText(count),
                     columnBounds.y + columnPadding);

        const auto cardArea = Rect {columnBounds.x + columnPadding,
                                    columnBounds.y + columnPadding + columnHeaderHeight,
                                    columnBounds.w - 2.0f * columnPadding,
                                    columnBounds.h - 2.0f * columnPadding - columnHeaderHeight};

        // Scrolls the column just far enough that the selected card is in view.
        auto offset = 0.0f;

        if (column == selectedColumn)
        {
            auto bottomOfSelected = 0.0f;

            for (std::size_t row = 0; row <= selectedRow && row < cards.size(); ++row)
                bottomOfSelected +=
                    paintCard(g, cards[row], cardArea, false, false) + cardGap;

            offset = std::max(0.0f, bottomOfSelected - cardArea.h);
        }

        auto top = cardArea.y - offset;

        for (std::size_t row = 0; row < cards.size(); ++row)
        {
            auto height = paintCard(g, cards[row], cardArea, false, false);
            auto isVisible = top >= cardArea.y && top + height <= cardArea.bottom();

            if (isVisible)
            {
                auto isSelected = column == selectedColumn && row == selectedRow;
                auto bounds = Rect {cardArea.x, top, cardArea.w, height};
                paintCard(g, cards[row], bounds, isSelected, true);
                placedCards.push_back({column, row, bounds});
            }

            top += height + cardGap;
        }
    }
}

// Too narrow for four columns side by side: one list, a section per filled
// column, scrolled to keep the selected card in view.
void TaskBoard::paintStacked(UI::Graphics& g, Rect area)
{
    const auto& zones = getColumnZonesInDisplayOrder();

    auto layOut = [&](bool isDrawn, float offset)
    {
        auto top = area.y - offset;
        auto bottomOfSelected = 0.0f;
        auto sectionTop = top;

        for (std::size_t column = 0; column < zones.size(); ++column)
        {
            const auto& cards = columns[column];

            if (cards.empty())
                continue;

            sectionTop = top;

            if (isDrawn && dragTargetZone == zones[column])
            {
                g.setColour(getZoneColour(zones[column]).withAlpha(dragHighlightAlpha));
                g.fillRoundedRect({area.x, top, area.w, columnHeaderHeight}, cardCornerRadius);
            }

            if (isDrawn && top >= area.y && top + columnHeaderHeight <= area.bottom())
            {
                g.setFont(titleFont);
                g.setColour(getZoneColour(zones[column]));
                drawTextLine(g,
                             std::string {getZoneName(zones[column])} + "  "
                                 + std::to_string(cards.size()),
                             area.x,
                             top);
            }

            top += columnHeaderHeight;

            for (std::size_t row = 0; row < cards.size(); ++row)
            {
                auto height = paintCard(g, cards[row], area, false, false);
                auto isSelected = column == selectedColumn && row == selectedRow;
                auto isVisible = top >= area.y && top + height <= area.bottom();

                if (isDrawn && isVisible)
                {
                    auto bounds = Rect {area.x, top, area.w, height};
                    paintCard(g, cards[row], bounds, isSelected, true);
                    placedCards.push_back({column, row, bounds});
                }

                top += height + cardGap;

                if (isSelected)
                    bottomOfSelected = top - area.y + offset;
            }

            if (isDrawn)
                placedColumns.push_back({column, {area.x, sectionTop, area.w, top - sectionTop}});

            top += sectionGap;
        }

        return bottomOfSelected;
    };

    auto bottomOfSelected = layOut(false, 0.0f);
    layOut(true, std::max(0.0f, bottomOfSelected - area.h));
}

std::optional<TaskBoard::CardAtPoint> TaskBoard::findCardAt(UI::Point point) const
{
    for (const auto& placed: placedCards)
    {
        if (placed.bounds.contains(point))
            return CardAtPoint {getColumnZonesInDisplayOrder()[placed.column],
                                columns[placed.column][placed.row].key};
    }

    return std::nullopt;
}

std::optional<Zone> TaskBoard::findColumnAt(UI::Point point) const
{
    for (const auto& placed: placedColumns)
    {
        if (placed.bounds.contains(point))
            return getColumnZonesInDisplayOrder()[placed.column];
    }

    return std::nullopt;
}

std::optional<std::size_t> TaskBoard::findPickerRowAt(UI::Point point) const
{
    for (std::size_t row = 0; row < placedPickerRows.size(); ++row)
    {
        if (placedPickerRows[row].contains(point))
            return row;
    }

    return std::nullopt;
}

std::optional<Rect> TaskBoard::findCardBounds(const std::string& key) const
{
    for (const auto& placed: placedCards)
    {
        if (columns[placed.column][placed.row].key == key)
            return placed.bounds;
    }

    return std::nullopt;
}

std::optional<Rect> TaskBoard::findColumnBounds(Zone zone) const
{
    for (const auto& placed: placedColumns)
    {
        if (getColumnZonesInDisplayOrder()[placed.column] == zone)
            return placed.bounds;
    }

    return std::nullopt;
}

void TaskBoard::selectCardAt(std::size_t column, std::size_t row)
{
    selectedColumn = column;
    selectedRow = row;
    selectedKey = columns[column][row].key;
    repaint();
}

void TaskBoard::mouseDown(const UI::MouseEvent& event)
{
    isDragging = false;
    dragTargetZone.reset();
    pressedCardKey.reset();

    if (mode == Mode::pullRequests)
    {
        pullRequestMouseDown(event);
        return;
    }

    if (mode == Mode::pickProject)
    {
        if (auto row = findPickerRowAt(event.position))
        {
            selectedProject = *row;
            chooseSelectedProject();
            repaint();
        }

        return;
    }

    if (mode != Mode::board)
        return;

    for (const auto& placed: placedCards)
    {
        if (!placed.bounds.contains(event.position))
            continue;

        isPressOnSelectedCard = placed.column == selectedColumn && placed.row == selectedRow;
        pressedCardKey = columns[placed.column][placed.row].key;
        selectCardAt(placed.column, placed.row);
        return;
    }
}

void TaskBoard::mouseDrag(const UI::MouseEvent& event)
{
    if (!pressedCardKey)
        return;

    auto travel = event.position - event.downPosition;
    auto distance = std::hypot(travel.x, travel.y);

    if (!isDragging && distance < dragStartDistance)
        return;

    isDragging = true;
    dragTargetZone = findColumnAt(event.position);
    repaint();
}

void TaskBoard::mouseUp(const UI::MouseEvent&)
{
    auto target = std::exchange(dragTargetZone, std::nullopt);
    auto wasDragging = std::exchange(isDragging, false);
    auto pressedKey = std::exchange(pressedCardKey, std::nullopt);

    if (!pressedKey)
        return;

    if (wasDragging)
    {
        if (target && isZoneJamieMayMoveTo(*target))
            moveSelectedCard(*target);

        repaint();
        return;
    }

    if (isPressOnSelectedCard)
        focusSelectedCard();
}

void TaskBoard::mouseDoubleClick(const UI::MouseEvent& event)
{
    if (mode == Mode::pullRequests)
    {
        for (std::size_t row = 0; row < placedPullRequestRows.size(); ++row)
        {
            if (placedPullRequestRows[row].contains(event.position))
            {
                selectedPullRequest = row;
                openSelectedPullRequestSession();
                return;
            }
        }

        return;
    }

    if (mode != Mode::board)
        return;

    for (const auto& placed: placedCards)
    {
        if (placed.bounds.contains(event.position))
        {
            selectCardAt(placed.column, placed.row);
            focusSelectedCard();
            return;
        }
    }
}
namespace
{
constexpr auto pullRequestRefreshSeconds = std::time_t {60};
constexpr auto pullRequestRowHeight = 64.0f;
constexpr auto pullRequestRowPadding = 6.0f;
constexpr auto pullRequestSecondLineOffset = 20.0f;
constexpr auto pullRequestThirdLineOffset = 38.0f;
constexpr auto successColourIndex = std::size_t {2};
constexpr auto failureColourIndex = std::size_t {1};
constexpr auto pendingColourIndex = std::size_t {3};

struct ChecksDescription
{
    std::string_view state;
    std::string_view shown;
    std::size_t ansiIndex;
};

constexpr auto checksDescriptions = std::array {
    ChecksDescription {"SUCCESS", "checks pass", successColourIndex},
    ChecksDescription {"FAILURE", "checks fail", failureColourIndex},
    ChecksDescription {"ERROR", "checks error", failureColourIndex},
    ChecksDescription {"PENDING", "checks running", pendingColourIndex},
    ChecksDescription {"EXPECTED", "checks waiting", pendingColourIndex},
};

struct ReviewDescription
{
    std::string_view decision;
    std::string_view shown;
};

constexpr auto reviewDescriptions = std::array {
    ReviewDescription {"APPROVED", "approved"},
    ReviewDescription {"CHANGES_REQUESTED", "changes requested"},
    ReviewDescription {"REVIEW_REQUIRED", "review required"},
};

std::string getShownChecks(const PullRequest& pullRequest)
{
    auto found = std::ranges::find(checksDescriptions, pullRequest.checksState, &ChecksDescription::state);

    if (found == checksDescriptions.end())
        return "no checks";

    return std::string {found->shown};
}

std::size_t getChecksColourIndex(const PullRequest& pullRequest)
{
    auto found = std::ranges::find(checksDescriptions, pullRequest.checksState, &ChecksDescription::state);

    if (found == checksDescriptions.end())
        return dimColourIndex;

    return found->ansiIndex;
}

std::string getShownReview(const PullRequest& pullRequest)
{
    auto found = std::ranges::find(reviewDescriptions, pullRequest.reviewDecision, &ReviewDescription::decision);

    if (found == reviewDescriptions.end())
        return "no review needed";

    return std::string {found->shown};
}

std::string getLastPathComponent(const std::string& path)
{
    auto separatorIndex = path.find_last_of('/');

    if (separatorIndex == std::string::npos)
        return path;

    return path.substr(separatorIndex + 1);
}
} // namespace

void TaskBoard::showPullRequests(PullRequestWindow window,
                                 const std::vector<LinkedPullRequest>& pullRequests)
{
    shown = true;
    mode = Mode::pullRequests;
    pullRequestWindow = window;
    pullRequestCaches[window] = {pullRequests, {}, std::time(nullptr)};
    selectedPullRequest = 0;
    repaint();
}

void TaskBoard::beginShowingPullRequests()
{
    mode = Mode::pullRequests;
    selectedPullRequest = 0;
    requestPullRequests();
    repaint();
}

// Cached per window for a minute; gh and the transcripts are read on a worker.
void TaskBoard::requestPullRequests()
{
    auto found = pullRequestCaches.find(pullRequestWindow);
    auto now = std::time(nullptr);
    auto isFresh = found != pullRequestCaches.end()
                   && now - found->second.fetchedAt < pullRequestRefreshSeconds;

    if (isFresh || isFetchingPullRequests || !startAgentSettings || !claudeQuery)
        return;

    isFetchingPullRequests = true;
    auto window = pullRequestWindow;
    auto settings = *startAgentSettings;
    auto configDirectory = claudeQuery->configDirectory;

    std::thread(
        [window, settings, configDirectory, now, guard = std::weak_ptr<bool> {alive}, this]
        {
            auto cache = PullRequestCache {};
            auto fetch = fetchPullRequests(settings.ghExecutable, window, now);
            cache.problem = fetch.problem;
            cache.fetchedAt = now;

            if (fetch.problem.empty())
            {
                auto sessions = collectRecentSessions(
                    configDirectory, settings.gitExecutable, getPullRequestWindowStart(window, now));
                cache.pullRequests = linkPullRequests(fetch.pullRequests, sessions);
            }

            Threads::callAsync(
                [window, cache = std::move(cache), guard, this]() mutable
                {
                    if (guard.expired())
                        return;

                    isFetchingPullRequests = false;
                    pullRequestCaches[window] = std::move(cache);
                    repaint();

                    if (window != pullRequestWindow)
                        requestPullRequests();
                });
        })
        .detach();
}

std::optional<TaskBoard::PullRequestCache> TaskBoard::getShownPullRequests() const
{
    auto found = pullRequestCaches.find(pullRequestWindow);

    if (found == pullRequestCaches.end())
        return std::nullopt;

    return found->second;
}

void TaskBoard::openSelectedPullRequestSession()
{
    const auto cache = getShownPullRequests();

    if (!cache || selectedPullRequest >= cache->pullRequests.size())
        return;

    const auto& linked = cache->pullRequests[selectedPullRequest];

    if (!linked.session)
        return;

    auto card = findCardOfSession(snapshot, linked.session->sessionId);
    auto session = *linked.session;
    close();

    if (card)
        onShowPane(card->paneShellId);
    else
        onResumeSession(session.workingDirectory, session.sessionId);
}

void TaskBoard::openSelectedPullRequestInBrowser()
{
    const auto cache = getShownPullRequests();

    if (cache && selectedPullRequest < cache->pullRequests.size())
        onOpenUrl(cache->pullRequests[selectedPullRequest].pullRequest.url);
}

bool TaskBoard::pullRequestKeyDown(const KeyEvent& event)
{
    const auto& characters = event.charactersIgnoringModifiers;
    const auto cache = getShownPullRequests();
    const auto count = !cache ? std::size_t {0} : cache->pullRequests.size();

    if (event.keyCode == KeyCode::Escape || characters == "p" || characters == "q")
    {
        mode = Mode::board;
    }
    else if (event.keyCode == KeyCode::Return)
    {
        openSelectedPullRequestSession();
        return true;
    }
    else if (characters == "o")
    {
        openSelectedPullRequestInBrowser();
    }
    else if (characters == "t")
    {
        pullRequestWindow = getNextPullRequestWindow(pullRequestWindow);
        selectedPullRequest = 0;
        requestPullRequests();
    }
    else if (event.keyCode == KeyCode::DownArrow || characters == "j")
    {
        if (selectedPullRequest + 1 < count)
            ++selectedPullRequest;
    }
    else if (event.keyCode == KeyCode::UpArrow || characters == "k")
    {
        if (selectedPullRequest > 0)
            --selectedPullRequest;
    }

    repaint();
    return true;
}

bool TaskBoard::pullRequestMouseDown(const UI::MouseEvent& event)
{
    for (std::size_t row = 0; row < placedPullRequestRows.size(); ++row)
    {
        if (!placedPullRequestRows[row].contains(event.position))
            continue;

        if (row == selectedPullRequest)
        {
            openSelectedPullRequestSession();
            return true;
        }

        selectedPullRequest = row;
        repaint();
        return true;
    }

    return false;
}

std::string TaskBoard::describeLinkedSession(const LinkedPullRequest& linked) const
{
    if (!linked.session || !linked.rule)
        return "no claude session found";

    auto card = findCardOfSession(snapshot, linked.session->sessionId);
    auto status = std::string {"ended: enter resumes it"};

    if (card)
        status = std::string {getZoneName(card->zone)};

    return std::string {getPullRequestLinkRuleName(*linked.rule)} + " → "
           + getLastPathComponent(linked.session->workingDirectory) + " (" + status + ")";
}

void TaskBoard::paintPullRequests(UI::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setColour(toColor(theme.background, backdropAlpha));
    g.fillRect(bounds);

    const auto width = bounds.w - 2.0f * outerMargin;
    const auto measure = [&g](std::string_view text) { return g.measureText(text); };
    const auto cache = getShownPullRequests();
    const auto dim = toColor(theme.ansi[dimColourIndex]);

    auto header = std::string {"pull requests"} + std::string {separator}
                  + std::string {getPullRequestWindowName(pullRequestWindow)};

    if (!cache)
        header += std::string {separator} + "asking gh…";
    else if (!cache->problem.empty())
        header += std::string {separator} + cache->problem;
    else
        header += std::string {separator} + std::to_string(cache->pullRequests.size()) + " open";

    g.setFont(headerFont);
    g.setColour(toColor(theme.foreground));
    auto headerLines = wrapTextToWidth(header, width, 1, measure);
    drawTextLine(g, headerLines.empty() ? std::string {} : headerLines.front(), outerMargin, headerTextTop);

    g.setFont(detailFont);
    g.setColour(dim);
    auto hintLines = wrapTextToWidth(
        "j/k move · enter go to its agent · o open in browser · t today/3 days/week · esc board",
        width,
        1,
        measure);
    drawTextLine(g,
                 hintLines.empty() ? std::string {} : hintLines.front(),
                 outerMargin,
                 bounds.h - footerHeight + footerTextTop);

    if (!cache)
        return;

    const auto listTop = headerHeight;
    const auto listBottom = bounds.h - footerHeight;
    const auto visibleRows = std::max<std::size_t>(1, (std::size_t) ((listBottom - listTop) / pullRequestRowHeight));
    const auto firstRow = selectedPullRequest >= visibleRows ? selectedPullRequest - visibleRows + 1 : 0;

    // placedPullRequestRows is indexed by pull request, so rows scrolled off stay empty rects.
    placedPullRequestRows.assign(cache->pullRequests.size(), Rect {});

    for (auto row = firstRow; row < cache->pullRequests.size() && row < firstRow + visibleRows; ++row)
    {
        const auto& linked = cache->pullRequests[row];
        const auto& pullRequest = linked.pullRequest;
        const auto top = listTop + (float) (row - firstRow) * pullRequestRowHeight;
        const auto rowBounds = Rect {outerMargin, top, width, pullRequestRowHeight - pullRequestRowPadding};
        placedPullRequestRows[row] = rowBounds;

        g.setColour(toColor(theme.background).brighter(cardBrightening));
        g.fillRoundedRect(rowBounds, cardCornerRadius);

        if (row == selectedPullRequest)
        {
            g.setColour(toColor(theme.foreground));
            g.drawRect(rowBounds, selectedBorderWidth);
        }

        const auto left = rowBounds.x + cardPadding;
        const auto textWidth = rowBounds.w - 2.0f * cardPadding;

        g.setFont(titleFont);
        g.setColour(toColor(theme.foreground));
        auto titleLines = wrapTextToWidth(
            pullRequest.repository + "#" + std::to_string(pullRequest.number) + "  " + pullRequest.title,
            textWidth,
            1,
            measure);
        drawTextLine(g, titleLines.empty() ? std::string {} : titleLines.front(), left, top + pullRequestRowPadding);

        g.setFont(detailFont);
        auto checks = getShownChecks(pullRequest) + std::string {separator};
        g.setColour(toColor(theme.ansi[getChecksColourIndex(pullRequest)]));
        drawTextLine(g, checks, left, top + pullRequestSecondLineOffset);

        auto checksWidth = g.measureText(checks);
        auto reviewLines = wrapTextToWidth(getShownReview(pullRequest), textWidth - checksWidth, 1, measure);
        g.setColour(dim);
        drawTextLine(g,
                     reviewLines.empty() ? std::string {} : reviewLines.front(),
                     left + checksWidth,
                     top + pullRequestSecondLineOffset);

        auto linkLines = wrapTextToWidth(describeLinkedSession(linked), textWidth, 1, measure);
        drawTextLine(g, linkLines.empty() ? std::string {} : linkLines.front(), left, top + pullRequestThirdLineOffset);
    }
}
} // namespace term
