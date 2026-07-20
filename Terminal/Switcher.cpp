#include "Switcher.h"

#include <eacp/Graphics/Primitives/TextMetrics.h>

#include <algorithm>

namespace term
{
using namespace eacp;
using Graphics::Color;
using Graphics::Context;
using Graphics::KeyEvent;
using Graphics::ModifierKeys;
using Graphics::MouseEvent;
using Graphics::Point;
using Graphics::Rect;
namespace KeyCode = Graphics::KeyCode;

namespace
{
constexpr float pad = 16.0f;
constexpr float gap = 10.0f;
constexpr float minCard = 104.0f;
constexpr float maxCard = 190.0f;
constexpr float cardHeight = 66.0f;
constexpr float panelHeight = pad * 2.0f + cardHeight + 30.0f;

std::string compactPath(std::string path)
{
    const auto home = FilePath::homeDirectory().str();

    if (!home.empty() && path.starts_with(home))
        path = "~" + path.substr(home.size());

    return path;
}

std::string truncated(const std::string& text, std::size_t max)
{
    if (text.size() <= max)
        return text;

    return text.substr(0, max - 1) + "…";
}

// Centre text horizontally on cx, drawn at baseline y.
void drawCentered(Context& context,
                  const std::string& text,
                  float cx,
                  float y,
                  const Graphics::Font& font)
{
    const auto w = Graphics::TextMetrics::measureWidth(text, font);
    context.drawText(text, {cx - w / 2.0f, y}, font);
}
} // namespace

Switcher::Switcher(const AppConfig& configToUse, SessionManager& sessionsToUse)
    : config(configToUse)
    , sessions(sessionsToUse)
    , theme(themeByName(configToUse.theme))
    , titleFont({config.font, 15.0f})
    , labelFont({config.font, 13.0f})
    , detailFont({config.font, 12.0f})
{
    setHandlesMouseEvents(true);
    setGrabsFocusOnMouseDown(true);
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

    sessions.beginPeek();
    peekSelected();
    return true;
}

void Switcher::step(bool reverse)
{
    const auto n = (int) order.size();

    if (n == 0)
        return;

    selected = (selected + (reverse ? -1 : 1) + n) % n;
    peekSelected();
    repaint();
}

void Switcher::peekSelected()
{
    if (selected >= 0 && selected < (int) order.size())
        sessions.peekTo(*order[(std::size_t) selected]);
}

void Switcher::commit()
{
    if (!shown)
        return;

    // The peek already put us on the highlight; commit it as a real switch so
    // it floats to the top of the MRU for next time.
    peekSelected();
    sessions.endPeek(true);
    shown = false;
    onClosed();
}

void Switcher::cancel()
{
    if (!shown)
        return;

    sessions.endPeek(false);
    shown = false;
    onClosed();
}

void Switcher::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        cancel();
        return;
    }

    if (event.keyCode == KeyCode::Return)
    {
        commit();
        return;
    }

    // Tab (with Ctrl still held) and the arrows walk the frozen order; Shift
    // reverses, mirroring Cmd+Shift+Tab.
    if (event.keyCode == KeyCode::Tab)
    {
        step(event.modifiers.shift);
        return;
    }

    if (event.keyCode == KeyCode::LeftArrow || event.keyCode == KeyCode::UpArrow)
    {
        step(true);
        return;
    }

    if (event.keyCode == KeyCode::RightArrow || event.keyCode == KeyCode::DownArrow)
    {
        step(false);
        return;
    }

    // Everything else is swallowed while the switcher owns the keyboard.
}

void Switcher::modifiersChanged(const ModifierKeys& modifiers)
{
    // The trigger let go — commit, exactly like releasing Cmd in the macOS app
    // switcher. Shift going up or down mid-walk must not commit, so key only on
    // Control.
    if (shown && !modifiers.control)
        commit();
}

void Switcher::mouseDown(const MouseEvent& event)
{
    if (const auto card = cardAt(event.pos); card >= 0)
    {
        selected = card;
        commit();
        return;
    }

    if (!panelBounds().contains(event.pos))
        cancel();
}

void Switcher::mouseMoved(const MouseEvent& event)
{
    if (const auto card = cardAt(event.pos); card >= 0 && card != selected)
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
    const auto avail = std::max(bounds.w - 120.0f, 240.0f) - pad * 2.0f;

    return std::clamp((avail - gap * (float) (n - 1)) / (float) n, minCard, maxCard);
}

Rect Switcher::panelBounds() const
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

int Switcher::cardAt(Point pos) const
{
    const auto panel = panelBounds();
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

void Switcher::paint(Context& context)
{
    if (order.empty())
        return;

    const auto panel = panelBounds();
    const auto cardW = cardWidth();

    context.setColor(Color::black(0.38f));
    context.fillRect(getLocalBounds());

    context.setColor(toColor(theme.background).brighter(0.05f));
    context.fillRoundedRect(panel, 14.0f);

    context.setColor(toColor(theme.selection, 0.8f));
    context.setLineWidth(1.0f);
    context.strokeRect(panel);

    auto x = panel.x + pad;
    const auto cardTop = panel.y + pad;

    for (auto i = 0; i < (int) order.size(); ++i)
    {
        auto* session = order[(std::size_t) i];
        const auto cardRect = Rect {x, cardTop, cardW, cardHeight};
        const auto cx = x + cardW / 2.0f;
        const auto highlighted = i == selected;
        const auto claude = session->isClaude();

        context.setColor(highlighted ? toColor(theme.selection, 0.9f)
                                      : toColor(theme.foreground, 0.06f));
        context.fillRoundedRect(cardRect, 8.0f);

        const auto icon = claude ? "✳" : "●";
        context.setColor(claude ? toColor(theme.ansi[5]) : toColor(theme.ansi[2]));
        drawCentered(context, icon, cx, cardTop + 26.0f, titleFont);

        const auto maxChars = (std::size_t) std::max(4, (int) (cardW / 8.5f));
        context.setColor(toColor(theme.foreground, highlighted ? 1.0f : 0.82f));
        drawCentered(
            context, truncated(session->name, maxChars), cx, cardTop + 50.0f, labelFont);

        x += cardW + gap;
    }

    // Detail line for the highlight: its conversation title (Claude) or cwd.
    if (selected >= 0 && selected < (int) order.size())
    {
        auto* session = order[(std::size_t) selected];
        const auto title = session->activeTitle();
        auto detail = session->isClaude() && !title.empty()
                          ? title
                          : compactPath(session->activeWorkingDirectory());

        if (const auto panes = session->view.paneCount(); panes > 1)
            detail += "  ·  " + std::to_string(panes) + " panes";

        context.setColor(toColor(theme.ansi[8]));
        drawCentered(context,
                     truncated(detail, 72),
                     panel.x + panel.w / 2.0f,
                     panel.y + panelHeight - 14.0f,
                     detailFont);
    }
}
} // namespace term
