#pragma once

#include "Config.h"
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

// What the palette, switcher, PR dashboard and Claude HUD share: the session
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
    void paintListBackdropAndCardWithBorder(eacp::UI::Graphics& g,
                                            eacp::UI::Rect panel,
                                            eacp::UI::Color border) const;
    void paintHeaderDivider(eacp::UI::Graphics& g,
                            eacp::UI::Rect panel,
                            float headerHeight) const;
    void paintSelectedRowHighlight(eacp::UI::Graphics& g, eacp::UI::Rect row) const;

    // Types into or erases from query; true when it changed. Typing moves the
    // selection back to the first row.
    bool editQueryText(std::string& query, const eacp::UI::KeyEvent& event);

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

    int selected = 0;
};
} // namespace term
