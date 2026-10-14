#include "PermissionsPanel.h"

#include <eacp/Graphics/Graphics.h>

#include <algorithm>

namespace term
{
using namespace eacp;
using permissions::Kind;
using permissions::Status;
using UI::Color;
using UI::KeyEvent;
using UI::MouseEvent;
using UI::Point;
using UI::Rect;
namespace KeyCode = UI::KeyCode;
namespace platform = permissions::platform;

namespace
{
constexpr float cardWidth = 720.0f;
constexpr float headerHeight = 96.0f;
constexpr float rowHeight = 42.0f;
constexpr float detailHeight = 132.0f;
constexpr float bannerHeight = 40.0f;
constexpr float footerHeight = 52.0f;

void drawText(UI::Graphics& g, const std::string& text, Point at, const UI::Font& font)
{
    g.setFont(font);
    g.drawText(text, at);
}

float measureWidth(UI::Graphics& g, const std::string& text, const UI::Font& font)
{
    g.setFont(font);
    return g.measureText(text);
}

bool isDone(Status status)
{
    return status == Status::granted;
}

std::vector<std::string> stepsFor(const permissions::Info& grant)
{
    if (grant.dragToList)
    {
        auto steps = std::vector<std::string> {
            "1. Start dragging the icon: System Settings opens at",
            "    Privacy & Security › " + grant.listName + ".",
            "2. Drop it into the list, or press + there and choose CowTerm.",
            "3. Turn CowTerm on. This row turns green by itself."};

        if (grant.kind == Kind::screenRecording)
            steps.push_back("4. Relaunch CowTerm (below); every shell keeps running.");

        return steps;
    }

    switch (grant.kind)
    {
        case Kind::automation:
            return {"1. Press Grant: macOS asks once for Finder, once for System Events.",
                    "2. Any other app is asked the first time a command scripts it.",
                    "3. Said no before? Grant opens Privacy & Security › Automation."};
        case Kind::filesAndFolders:
            return {"1. Press Grant: macOS asks for Desktop, Documents, Downloads.",
                    "2. Full Disk Access covers all three, and more.",
                    "3. Said no before? Grant opens Privacy & Security › Files & Folders."};
        case Kind::notifications:
            return {"1. Press Grant for macOS's prompt.",
                    "2. Said no before? Grant opens Notifications › CowTerm.",
                    "3. Reset all permissions leaves notifications alone."};
        default:
            return {};
    }
}
} // namespace

PermissionsPanel::PermissionsPanel(const AppConfig& configToUse)
    : config(configToUse)
    , theme(themeByName(configToUse.theme))
    , titleFont({config.font, 18.0f})
    , rowFont({config.font, 13.0f})
    , detailFont({config.font, 11.0f})
{
    setInterceptsMouseClicks(true);
    setWantsKeyboardFocus(true);
}

PermissionsPanel::~PermissionsPanel()
{
    *alive = false;
}

bool PermissionsPanel::requiredMissingNow()
{
    // A fresh process, so its own Screen Recording answer is current.
    auto entries = std::vector<permissions::Entry> {};

    for (const auto& grant: permissions::all())
        if (grant.required)
            entries.push_back({grant.kind, platform::check(grant.kind)});

    return permissions::requiredMissing(entries);
}

void PermissionsPanel::show(bool automaticToUse, bool confirmReset)
{
    automatic = automaticToUse;
    confirmingReset = confirmReset;
    error.clear();
    shown = true;
    selected = 0;
    ticks = 0;

    refresh();

    // The first row that still needs something.
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (!isDone(entries[i].status))
        {
            selected = (int) i;
            break;
        }

    tick = std::make_unique<Threads::Timer>(
        [this]
        {
            ++ticks;
            refresh();
        },
        Time::MS {1000});

    repaint();
}

void PermissionsPanel::close()
{
    shown = false;
    confirmingReset = false;
    tick.reset();
    onClosed();
}

Status PermissionsPanel::statusOf(Kind kind) const
{
    for (const auto& entry: entries)
        if (entry.kind == kind)
            return entry.status;

    return Status::unknown;
}

const permissions::Info& PermissionsPanel::selectedInfo() const
{
    return permissions::all()[(std::size_t) selected];
}

bool PermissionsPanel::bannerShown() const
{
    return resetDone || statusOf(Kind::screenRecording) == Status::needsRelaunch
           || selectedInfo().kind == Kind::screenRecording;
}

void PermissionsPanel::refresh()
{
    if (!shown)
        return;

    entries.clear();

    for (const auto& grant: permissions::all())
    {
        auto status = platform::check(grant.kind);

        // This process caches its Screen Recording answer, so a grant made
        // while it runs (or a reset) shows only in a fresh process.
        if (grant.kind == Kind::screenRecording
            && (status != Status::granted || resetDone))
            status = screenProbe.value_or(false) ? Status::needsRelaunch
                                                 : Status::notGranted;

        entries.push_back({grant.kind, status});
    }

    // The probe spawns a process, so every other tick, and only while the
    // in-process answer can't be trusted.
    const auto screenDoubt = statusOf(Kind::screenRecording) != Status::granted;

    if (screenDoubt && !probing && ticks % 2 == 0)
    {
        probing = true;
        platform::probeScreenRecording(
            [this, alive = alive](bool granted)
            {
                if (!*alive)
                    return;

                probing = false;

                if (screenProbe != granted)
                {
                    screenProbe = granted;
                    refresh();
                }
            });
    }

    // Launch's showing goes away once nothing required is missing, as
    // Rudder's unlock card does.
    if (automatic && permissions::requiredSatisfied(entries))
    {
        close();
        return;
    }

    repaint();
}

void PermissionsPanel::act(int row)
{
    const auto& grant = permissions::all()[(std::size_t) row];
    const auto status = statusOf(grant.kind);

    if (status == Status::granted)
        return;

    if (status == Status::needsRelaunch)
    {
        platform::relaunch();
        return;
    }

    const auto wasPrompted = prompted.contains(grant.kind);
    prompted.insert(grant.kind);

    platform::request(grant.kind,
                      wasPrompted,
                      [this, alive = alive]
                      {
                          if (*alive)
                              refresh();
                      });
    repaint();
}

void PermissionsPanel::resetAll()
{
    confirmingReset = false;
    error.clear();

    if (!platform::reset("All", error))
    {
        repaint();
        return;
    }

    // macOS forgets them all, so it will prompt again; this process doesn't.
    resetDone = true;
    automatic = false;
    prompted.clear();
    screenProbe = false;
    refresh();
}

PermissionsPanel::Layout PermissionsPanel::layout() const
{
    const auto bounds = getLocalBounds();
    const auto count = (float) permissions::all().size();
    const auto width = std::min(cardWidth, bounds.w - 40.0f);
    const auto height = headerHeight + count * rowHeight + 8.0f + detailHeight
                        + (bannerShown() ? bannerHeight + 8.0f : 0.0f) + footerHeight;

    auto at = Layout {};
    at.card = {(bounds.w - width) / 2.0f,
               std::max((bounds.h - height) / 2.0f, 12.0f),
               width,
               height};

    auto y = at.card.y + headerHeight;

    for (auto i = 0; i < (int) count; ++i)
    {
        const auto row = Rect {at.card.x + 12.0f, y, at.card.w - 24.0f, rowHeight - 2.0f};
        at.rows.push_back(row);
        at.actions.push_back({row.right() - 136.0f, row.y + 7.0f, 124.0f, 26.0f});
        y += rowHeight;
    }

    y += 8.0f;
    at.detail = {at.card.x + 12.0f, y, at.card.w - 24.0f, detailHeight};
    at.tile = {at.detail.x + 16.0f, at.detail.y + 10.0f, 200.0f, 112.0f};
    y += detailHeight;

    if (bannerShown())
    {
        y += 8.0f;
        at.banner = {at.card.x + 12.0f, y, at.card.w - 24.0f, bannerHeight};
        at.relaunch = {at.banner.right() - 172.0f, at.banner.y + 7.0f, 160.0f, 26.0f};
        y += bannerHeight;
    }

    const auto footer = at.card.bottom() - footerHeight + 12.0f;
    at.reset = {at.card.x + 24.0f, footer, 200.0f, 28.0f};
    at.cancel = {at.card.right() - 24.0f - 90.0f, footer, 90.0f, 28.0f};
    at.confirm = {at.cancel.x - 8.0f - 90.0f, footer, 90.0f, 28.0f};
    return at;
}

bool PermissionsPanel::keyDown(const KeyEvent& event)
{
    if (event.keyCode == KeyCode::Escape)
    {
        if (confirmingReset)
        {
            confirmingReset = false;
            repaint();
        }
        else
            close();

        return true;
    }

    if (event.keyCode == KeyCode::Return)
    {
        if (confirmingReset)
            resetAll();
        else
            act(selected);

        return true;
    }

    const auto& chars = event.charactersIgnoringModifiers;
    const auto count = (int) permissions::all().size();

    if (event.keyCode == KeyCode::UpArrow || chars == "k"
        || (event.modifiers.control && chars == "p"))
    {
        selected = (selected - 1 + count) % count;
        repaint();
        return true;
    }

    if (event.keyCode == KeyCode::DownArrow || chars == "j"
        || (event.modifiers.control && chars == "n"))
    {
        selected = (selected + 1) % count;
        repaint();
        return true;
    }

    // Everything else is swallowed while the panel owns the keyboard.
    return true;
}

void PermissionsPanel::mouseMove(const MouseEvent& event)
{
    mouse = event.position;
    repaint();
}

void PermissionsPanel::mouseDown(const MouseEvent& event)
{
    const auto at = layout();
    const auto& pos = event.position;

    if (!at.card.contains(pos))
    {
        close();
        return;
    }

    if (confirmingReset)
    {
        if (at.confirm.contains(pos))
            resetAll();
        else if (at.cancel.contains(pos))
        {
            confirmingReset = false;
            repaint();
        }

        return;
    }

    if (at.reset.contains(pos))
    {
        confirmingReset = true;
        repaint();
        return;
    }

    if (bannerShown() && at.relaunch.contains(pos))
    {
        platform::relaunch();
        return;
    }

    for (std::size_t i = 0; i < at.rows.size(); ++i)
    {
        if (!at.rows[i].contains(pos))
            continue;

        selected = (int) i;

        if (at.actions[i].contains(pos))
            act((int) i);

        repaint();
        return;
    }

    // Starting the drag opens the pane, so the list is there to drop on.
    if (selectedInfo().dragToList && !isDone(statusOf(selectedInfo().kind))
        && at.tile.contains(pos))
    {
        if (auto* host = getHost())
            platform::startAppDrag(*host);

        platform::openSettings(selectedInfo().kind);
    }
}

void PermissionsPanel::paintButton(UI::Graphics& g,
                                   const Rect& area,
                                   const std::string& label,
                                   Color accent)
{
    const auto hovered = area.contains(mouse);
    g.setColour(accent.withAlpha(hovered ? 0.28f : 0.14f));
    g.fillRoundedRect(area, 6.0f);
    g.setColour(accent.withAlpha(0.7f));
    g.drawRoundedRect(area, 6.0f, 1.0f);
    g.setColour(toColor(theme.foreground));
    g.setFont(detailFont);
    g.drawText(label, area, UI::Justification::Centred);
}

void PermissionsPanel::paintRow(UI::Graphics& g, const Layout& at, int i)
{
    const auto& grant = permissions::all()[(std::size_t) i];
    const auto status = statusOf(grant.kind);
    const auto& row = at.rows[(std::size_t) i];

    if (i == selected)
    {
        g.setColour(toColor(theme.selection, 0.7f));
        g.fillRoundedRect(row, 6.0f);
    }

    const auto colour = [&]
    {
        switch (status)
        {
            case Status::granted:
                return toColor(theme.ansi[2]);
            case Status::needsRelaunch:
                return toColor(theme.ansi[3]);
            case Status::denied:
                return toColor(theme.ansi[1]);
            case Status::unknown:
                return toColor(theme.ansi[7]);
            case Status::notGranted:
                break;
        }

        return grant.required ? toColor(theme.ansi[1]) : toColor(theme.ansi[3]);
    }();

    g.setColour(colour);
    g.fillRoundedRect({row.x + 10.0f, row.y + 11.0f, 8.0f, 8.0f}, 4.0f);

    g.setColour(toColor(theme.foreground));
    const auto nameWidth = measureWidth(g, grant.name, rowFont);
    drawText(g, grant.name, {row.x + 26.0f, row.y + 18.0f}, rowFont);

    // Required vs optional, beside the name.
    const auto tag = grant.required ? std::string {"required"} : std::string {"optional"};
    const auto tagWidth = measureWidth(g, tag, detailFont) + 12.0f;
    const auto tagRect = Rect {row.x + 26.0f + nameWidth + 10.0f, row.y + 5.0f, tagWidth, 16.0f};
    const auto tagColour = grant.required ? toColor(theme.ansi[5]) : toColor(theme.ansi[8]);
    g.setColour(tagColour.withAlpha(0.25f));
    g.fillRoundedRect(tagRect, 8.0f);
    g.setColour(grant.required ? toColor(theme.ansi[5]) : toColor(theme.ansi[7]));
    g.setFont(detailFont);
    g.drawText(tag, tagRect, UI::Justification::Centred);

    const auto& action = at.actions[(std::size_t) i];

    // Kept clear of the button on a narrow window.
    g.saveState();
    g.reduceClipRegion({row.x, row.y, action.x - 8.0f - row.x, row.h});
    g.setColour(toColor(theme.ansi[7]));
    drawText(g, grant.why, {row.x + 26.0f, row.y + 34.0f}, detailFont);
    g.restoreState();

    const auto label = permissions::statusLabel(status);
    const auto labelWidth = measureWidth(g, label, detailFont);
    g.setColour(colour);
    drawText(g, label, {action.x - 12.0f - labelWidth, row.y + 18.0f}, detailFont);

    const auto button =
        permissions::actionLabel(grant, status, prompted.contains(grant.kind));

    if (!button.empty())
        paintButton(g, action, button, toColor(theme.ansi[4]));
}

void PermissionsPanel::paintDetail(UI::Graphics& g, const Layout& at)
{
    const auto& grant = selectedInfo();
    const auto done = isDone(statusOf(grant.kind));
    auto x = at.detail.x + 24.0f;

    if (grant.dragToList)
    {
        // Rudder's tile: the app's icon in a green-edged card, dragged from.
        const auto& tile = at.tile;
        g.setColour(Color(1.f, 1.f, 1.f, tile.contains(mouse) && !done ? 0.08f : 0.04f));
        g.fillRoundedRect(tile, 14.0f);
        g.setColour(toColor(theme.ansi[2], done ? 0.35f : 0.8f));
        g.drawRoundedRect(tile, 14.0f, 1.0f);

        if (icon == nullptr)
            if (auto* host = getHost())
                if (auto image = platform::appIcon())
                    icon = host->getImageCache().get(image);

        if (icon != nullptr)
            g.drawImage(icon,
                        {tile.x + (tile.w - 64.0f) / 2.0f, tile.y + 14.0f, 64.0f, 64.0f},
                        done ? 0.4f : 1.0f);

        g.setColour(toColor(theme.foreground));
        g.setFont(detailFont);
        g.drawText(done ? "CowTerm is in the list" : "Drag CowTerm into the list",
                   {tile.x, tile.y + 84.0f, tile.w, 20.0f},
                   UI::Justification::Centred);

        x = tile.right() + 24.0f;
    }

    auto y = at.detail.y + 26.0f;

    g.setColour(toColor(theme.foreground));
    drawText(g, grant.name, {x, y}, rowFont);
    y += 22.0f;

    g.setColour(toColor(theme.ansi[7]));

    for (const auto& step: stepsFor(grant))
    {
        drawText(g, step, {x, y}, detailFont);
        y += 17.0f;
    }
}

void PermissionsPanel::paint(UI::Graphics& g)
{
    const auto at = layout();
    const auto& card = at.card;

    g.setColour(Color::black(0.5f));
    g.fillRect(getLocalBounds());

    g.setColour(toColor(theme.background).brighter(0.05f));
    g.fillRoundedRect(card, 22.0f);
    g.setColour(toColor(theme.selection, 0.8f));
    g.drawRoundedRect(card, 22.0f, 1.0f);

    const auto missing = permissions::requiredMissing(entries);

    g.setColour(toColor(theme.foreground));
    g.setFont(titleFont);
    g.setFontStyle(UI::FontStyle::Bold);
    g.drawText(missing ? "Unlock CowTerm" : "CowTerm permissions",
               {card.x, card.y + 18.0f, card.w, 26.0f},
               UI::Justification::Centred);

    g.setColour(toColor(theme.ansi[7]));
    g.setFont(detailFont);
    g.drawText("Commands you run in CowTerm get CowTerm's macOS privacy grants, so agents",
               {card.x, card.y + 50.0f, card.w, 16.0f},
               UI::Justification::Centred);
    g.drawText("and scripts can only screenshot, drive apps or read ~/Library if CowTerm can.",
               {card.x, card.y + 66.0f, card.w, 16.0f},
               UI::Justification::Centred);

    for (auto i = 0; i < (int) at.rows.size(); ++i)
        paintRow(g, at, i);

    g.setColour(toColor(theme.selection));
    g.drawLine({at.detail.x + 4.0f, at.detail.y - 4.0f},
               {at.detail.right() - 4.0f, at.detail.y - 4.0f});

    paintDetail(g, at);

    if (bannerShown())
    {
        const auto& banner = at.banner;
        g.setColour(toColor(theme.ansi[3], 0.12f));
        g.fillRoundedRect(banner, 8.0f);

        const auto line = resetDone
                              ? "Grants reset. Relaunch so CowTerm drops the ones it cached."
                          : statusOf(Kind::screenRecording) == Status::needsRelaunch
                              ? "Screen Recording is on; CowTerm sees it after a relaunch."
                              : "Screen Recording only takes effect after CowTerm relaunches.";

        g.setColour(toColor(theme.ansi[3]));
        drawText(g, line, {banner.x + 14.0f, banner.y + 17.0f}, detailFont);
        g.setColour(toColor(theme.ansi[7]));
        drawText(g,
                 "Relaunching keeps every shell running (the daemon holds them).",
                 {banner.x + 14.0f, banner.y + 32.0f},
                 detailFont);

        paintButton(g, at.relaunch, "Relaunch CowTerm", toColor(theme.ansi[3]));
    }

    const auto footerText = card.bottom() - footerHeight + 30.0f;

    if (confirmingReset)
    {
        g.setColour(toColor(theme.ansi[1]));
        drawText(g,
                 "Reset every privacy grant CowTerm has? macOS will ask again.",
                 {card.x + 24.0f, footerText},
                 detailFont);
        paintButton(g, at.confirm, "Reset", toColor(theme.ansi[1]));
        paintButton(g, at.cancel, "Cancel", toColor(theme.ansi[8]));
        return;
    }

    paintButton(g, at.reset, "Reset all permissions…", toColor(theme.ansi[1]));

    const auto hint = error.empty() ? std::string {"↑↓ select · enter grant · esc close"}
                                    : "tccutil: " + error;
    const auto width = measureWidth(g, hint, detailFont);
    g.setColour(error.empty() ? toColor(theme.ansi[8]) : toColor(theme.ansi[1]));
    drawText(g, hint, {card.right() - 24.0f - width, footerText}, detailFont);
}
} // namespace term
