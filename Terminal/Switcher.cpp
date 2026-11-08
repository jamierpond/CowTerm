#include "Switcher.h"

#include "TextLayout.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/Graphics/Keyboard.h>

#include <algorithm>

namespace term
{
using namespace eacp;
using UI::Color;
using UI::KeyEvent;
using UI::MouseEvent;
using UI::Point;
using UI::Rect;
namespace KeyCode = UI::KeyCode;

namespace
{
constexpr float pad = 16.0f;
constexpr float gap = 10.0f;
constexpr float minCard = 104.0f;
constexpr float maxCard = 190.0f;
constexpr float cardHeight = 66.0f;
constexpr float panelHeight = pad * 2.0f + cardHeight + 30.0f;
constexpr auto fontSizes = OverlayFontSizes {.header = 15.0f, .row = 13.0f, .detail = 12.0f};
constexpr auto cardStyle =
    OverlayCardStyle {.backgroundBrightening = 0.05f, .cornerRadius = 14.0f};
constexpr auto cardBorderAlpha = 0.8f;
constexpr auto modifierPollIntervalMs = 60;
constexpr auto windowSideMargin = 120.0f;
constexpr auto minimumAvailableWidth = 240.0f;
constexpr auto cardCornerRadius = 8.0f;
constexpr auto highlightedCardAlpha = 0.9f;
constexpr auto plainCardAlpha = 0.06f;
constexpr auto iconBaseline = 26.0f;
constexpr auto nameBaseline = 50.0f;
constexpr auto minimumNameLength = 4;
constexpr auto averageNameCharacterWidth = 8.5f;
constexpr auto highlightedNameAlpha = 1.0f;
constexpr auto plainNameAlpha = 0.82f;
constexpr auto detailLength = 72;
constexpr auto detailBottomOffset = 14.0f;

// Centre text horizontally on anchor.x, drawn at baseline anchor.y.
void drawCentered(UI::Graphics& g, const std::string& text, Point anchor, const UI::Font& font)
{
    const auto w = measureTextInFont(g, text, font);
    g.drawText(text, {anchor.x - w / 2.0f, anchor.y});
}
} // namespace

Switcher::Switcher(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : OverlayPanel(configToUse, sessionsToUse, fontSizes)
{
}

bool Switcher::begin(bool reverse)
{
    order.clear();

    for (auto& session: sessions.all())
        order.push_back(session.get());

    // Nothing to flip between; leave the switcher closed.
    if (order.size() < 2)
        return false;

    // Freeze the order most-recently-used first. The active session was just
    // touched, so it sorts to index 0 and the "previous" one lands at index 1.
    std::stable_sort(order.begin(),
                     order.end(),
                     [this](TermSession* a, TermSession* b)
                     { return sessions.lastUsed(a->key()) > sessions.lastUsed(b->key()); });

    const auto n = (int) order.size();
    selected = reverse ? n - 1 : 1;
    shown = true;

    // Watch the trigger: once Ctrl comes back up, commit the highlight, exactly
    // like releasing Cmd in the macOS app switcher. Shift going up or down
    // mid-walk must not commit, so we key only on Control.
    modifierPoll = std::make_unique<Threads::Timer>(
        [this]
        {
            if (shown && !Graphics::Keyboard::isControlPressed())
                chooseSelected();
        },
        modifierPollIntervalMs);

    sessions.beginPeek();
    peekSelected();
    return true;
}

void Switcher::step(bool reverse)
{
    const auto n = (int) order.size();

    if (n == 0)
        return;

    const auto delta = reverse ? -1 : 1;
    selected = wrapSelection(selected, delta, n);
    peekSelected();
    repaint();
}

void Switcher::peekSelected()
{
    if (selected >= 0 && selected < (int) order.size())
        sessions.peekTo(*order[(std::size_t) selected]);
}

void Switcher::chooseSelected()
{
    if (!shown)
        return;

    // The peek already put us on the highlight; commit it as a real switch so
    // it floats to the top of the MRU for next time.
    peekSelected();
    sessions.endPeek(true);
    shown = false;
    stopPolling();
    onClosed();
}

void Switcher::dismiss()
{
    if (!shown)
        return;

    sessions.endPeek(false);
    shown = false;
    stopPolling();
    onClosed();
}

void Switcher::stopPolling()
{
    // chooseSelected()/dismiss() can run from inside the poll timer's own callback, so
    // tearing the Timer down here would free the closure mid-call. Defer the
    // reset to the next loop turn — and skip it if the switcher has reopened in
    // the meantime, which owns a fresh timer we must not drop.
    Threads::callAsync(
        [this]
        {
            if (!shown)
                modifierPoll.reset();
        });
}

bool Switcher::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        dismiss();
        return true;
    }

    if (event.keyCode == KeyCode::Return)
    {
        chooseSelected();
        return true;
    }

    // Tab (with Ctrl still held) and the arrows walk the frozen order; Shift
    // reverses, mirroring Cmd+Shift+Tab.
    if (event.keyCode == KeyCode::Tab)
    {
        step(event.modifiers.shift);
        return true;
    }

    if (event.keyCode == KeyCode::LeftArrow || event.keyCode == KeyCode::UpArrow)
    {
        step(true);
        return true;
    }

    if (event.keyCode == KeyCode::RightArrow || event.keyCode == KeyCode::DownArrow)
    {
        step(false);
        return true;
    }

    // Everything else is swallowed while the switcher owns the keyboard.
    return true;
}

void Switcher::mouseMove(const MouseEvent& event)
{
    if (const auto card = findRowAt(event.position); card >= 0 && card != selected)
    {
        selected = card;
        peekSelected();
        repaint();
    }
}

float Switcher::cardWidth() const
{
    const auto bounds = getLocalBounds();
    const auto n = std::max((int) order.size(), 1);
    const auto avail = std::max(bounds.w - windowSideMargin, minimumAvailableWidth) - pad * 2.0f;

    return std::clamp((avail - gap * (float) (n - 1)) / (float) n, minCard, maxCard);
}

Rect Switcher::calculatePanelBounds() const
{
    const auto bounds = getLocalBounds();
    const auto n = std::max((int) order.size(), 1);
    const auto cardW = cardWidth();
    const auto width = pad * 2.0f + cardW * (float) n + gap * (float) (n - 1);

    return {(bounds.w - width) / 2.0f,
            (bounds.h - panelHeight) / 2.0f,
            width,
            panelHeight};
}

int Switcher::findRowAt(Point pos) const
{
    const auto panel = calculatePanelBounds();
    const auto cardW = cardWidth();
    const auto top = panel.y + pad;

    if (pos.y < top || pos.y > top + cardHeight)
        return -1;

    auto x = panel.x + pad;

    for (auto i = 0; i < (int) order.size(); ++i)
    {
        if (pos.x >= x && pos.x <= x + cardW)
            return i;

        x += cardW + gap;
    }

    return -1;
}

std::string Switcher::describeSession(const TermSession& session) const
{
    const auto title = session.activeTitle();
    auto detail = title;

    if (!session.isClaude() || title.empty())
        detail = compactHomePath(session.activeWorkingDirectory());

    if (const auto panes = session.view.paneCount(); panes > 1)
        detail += "  ·  " + std::to_string(panes) + " panes";

    return detail;
}

void Switcher::paintCard(UI::Graphics& g, int index, Rect card)
{
    const auto& session = *order[(std::size_t) index];
    const auto cx = card.x + card.w / 2.0f;
    const auto highlighted = index == selected;
    const auto claude = session.isClaude();

    if (highlighted)
        g.setColour(toColor(theme.selection, highlightedCardAlpha));
    else
        g.setColour(toColor(theme.foreground, plainCardAlpha));

    g.fillRoundedRect(card, cardCornerRadius);

    const auto icon = claude ? "✳" : "●";
    const auto iconColour = claude ? theme.ansi[5] : theme.ansi[2];
    g.setColour(toColor(iconColour));
    drawCentered(g, icon, {cx, card.y + iconBaseline}, headerFont);

    const auto maxChars = (std::size_t) std::max(
        minimumNameLength, (int) (card.w / averageNameCharacterWidth));
    const auto nameAlpha = highlighted ? highlightedNameAlpha : plainNameAlpha;
    g.setColour(toColor(theme.foreground, nameAlpha));
    drawCentered(
        g, truncateWithEllipsis(session.name, maxChars), {cx, card.y + nameBaseline}, rowFont);
}

// The highlight's conversation title (Claude) or cwd.
void Switcher::paintSelectedDetail(UI::Graphics& g, Rect panel)
{
    if (selected < 0 || selected >= (int) order.size())
        return;

    const auto detail = describeSession(*order[(std::size_t) selected]);

    g.setColour(toColor(theme.ansi[8]));
    drawCentered(g,
                 truncateWithEllipsis(detail, detailLength),
                 {panel.x + panel.w / 2.0f, panel.y + panelHeight - detailBottomOffset},
                 detailFont);
}

void Switcher::paint(UI::Graphics& g)
{
    if (order.empty())
        return;

    const auto panel = calculatePanelBounds();
    const auto cardW = cardWidth();

    paintBackdropAndCard(g, panel, toColor(theme.selection, cardBorderAlpha), cardStyle);

    auto x = panel.x + pad;

    for (auto i = 0; i < (int) order.size(); ++i)
    {
        paintCard(g, i, {x, panel.y + pad, cardW, cardHeight});
        x += cardW + gap;
    }

    paintSelectedDetail(g, panel);
}
} // namespace term
