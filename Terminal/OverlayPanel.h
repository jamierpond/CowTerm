#pragma once

#include "Multiplexer/Config.h"
#include "Multiplexer/HostColours.h"
#include "Multiplexer/UnreadMark.h"
#include "SearchField.h"
#include "Session.h"

#include <eacp/UI/UI.h>

#include <string>

namespace term
{
struct OverlayFontSizes
{
    float header = 0.0f;
    float row = 0.0f;
    float detail = 0.0f;
};

// The shape of a centred list overlay: a header, up to maximumRows rows, an
// optional hint line, placed topFraction of the way down the window.
struct ListPanelMetrics
{
    float maximumWidth = 0.0f;
    float rowHeight = 0.0f;
    float headerHeight = 0.0f;
    float hintHeight = 0.0f;
    int maximumRows = 0;
    float topFraction = 0.0f;
};

struct OverlayCardStyle
{
    float backgroundBrightening = 0.0f;
    float cornerRadius = 0.0f;
};

int countVisibleListRows(const ListPanelMetrics& metrics, int itemCount);
eacp::UI::Rect calculateListPanelBounds(eacp::UI::Rect area,
                                        const ListPanelMetrics& metrics,
                                        int itemCount);
float calculateListRowTop(eacp::UI::Rect panel, const ListPanelMetrics& metrics, int row);

// The row under position, or -1 when it is outside the panel's rows.
int findListRowAt(eacp::UI::Rect panel,
                  const ListPanelMetrics& metrics,
                  int itemCount,
                  eacp::UI::Point position);

int wrapSelection(int selected, int delta, int count);

bool isSelectPreviousKey(const eacp::UI::KeyEvent& event);
bool isSelectNextKey(const eacp::UI::KeyEvent& event);

// The home directory shown as "~".
std::string compactHomePath(std::string path);

void drawTextInFont(eacp::UI::Graphics& g,
                    const std::string& text,
                    eacp::UI::Point position,
                    const eacp::UI::Font& font);
float measureTextInFont(eacp::UI::Graphics& g,
                        const std::string& text,
                        const eacp::UI::Font& font);

// The host's colour across a whole row or card, at full strength down its left
// edge, as the phone draws its session rows.
void paintHostFill(eacp::UI::Graphics& g, const HostRowColours& colours, eacp::UI::Rect area, float cornerRadius);

// The highlighted row or card gets an outline in its text colour, so it stands
// out without telling one colour from another.
void paintHighlightOutline(eacp::UI::Graphics& g,
                           const HostRowColours& colours,
                           eacp::UI::Rect area,
                           float cornerRadius);

// Where the unread pill goes: its right edge, the middle of its height, and
// its height.
struct UnreadPillPlace
{
    float right = 0.0f;
    float middle = 0.0f;
    float height = 0.0f;
};

float measureUnreadPillWidth(eacp::UI::Graphics& g, const UnreadMark& mark, const eacp::UI::Font& font);

// The unread mark (UnreadMark.h) as a yellow pill naming the event in black
// text, the same mark the phone draws. Returns its width.
float paintUnreadPill(eacp::UI::Graphics& g,
                      const UnreadMark& mark,
                      UnreadPillPlace place,
                      const eacp::UI::Font& font);

// What the palette and switcher share: the session
// list they drive, the theme, three fonts, and the dimmed backdrop and card
// they paint over the terminal.
class OverlayPanel : public eacp::UI::Component
{
public:
    // A click on a row chooses it; one outside the panel dismisses.
    void mouseDown(const eacp::UI::MouseEvent& event) override;

protected:
    OverlayPanel(const AppConfig& configToUse,
                 SessionManager& sessionsToUse,
                 OverlayFontSizes fontSizes);

    void paintBackdropAndCard(eacp::UI::Graphics& g,
                              eacp::UI::Rect panel,
                              eacp::UI::Color border,
                              OverlayCardStyle style) const;
    void paintListBackdropAndCard(eacp::UI::Graphics& g, eacp::UI::Rect panel) const;
    void paintHeaderDivider(eacp::UI::Graphics& g,
                            eacp::UI::Rect panel,
                            float headerHeight) const;

    // A row's or card's colours for its host; an empty host is this machine.
    HostRowColours getHostRowColoursFor(const std::string& host) const;

    virtual eacp::UI::Rect calculatePanelBounds() const = 0;

    // The row under position, or -1 when there is none.
    virtual int findRowAt(eacp::UI::Point position) const = 0;
    virtual void chooseSelected() = 0;
    virtual void dismiss() = 0;

    const AppConfig& config;
    SessionManager& sessions;
    Theme theme;

    eacp::UI::Font headerFont;
    eacp::UI::Font rowFont;
    eacp::UI::Font detailFont;

    const std::string localHostName;

    int selected = 0;
};

// An overlay with a query box over its rows (the palette),
// driven by Rudder's vim modes.
class SearchablePanel : public OverlayPanel
{
protected:
    using OverlayPanel::OverlayPanel;

    // Runs a key through the search field's vim modes and performs its action;
    // false when the panel should handle the key itself.
    bool handleSearchFieldKey(const eacp::UI::KeyEvent& event);

    // "› query" with a thin caret in insert mode, or a block caret and a
    // NORMAL label in normal mode.
    void paintSearchField(eacp::UI::Graphics& g, eacp::UI::Point baselineStart);

    void performSearchAction(searchkeys::Action action);
    void moveSelectionForSearchAction(searchkeys::Action action);

    virtual int countRows() const = 0;
    virtual void moveSelection(int delta) = 0;
    virtual void searchTextChanged() = 0;

    SearchField searchField;
};
} // namespace term
