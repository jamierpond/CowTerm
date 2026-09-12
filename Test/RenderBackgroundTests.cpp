#include <CowTermTerminal/TerminalView.h>

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/GPU/GPU.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <string>

// Cell backgrounds, checked where they actually go wrong: the pixels.
//
// The screen model can hold a perfectly good background colour and nothing
// reach the glass -- the renderer queues backgrounds as sprite quads, and a
// queue is discarded by the next begin() and drawn only when the pass ends.
// Get either wrong and every selected row, every diff line, every lazygit
// highlight renders as bare text, with nothing in the model to show for it.
// ScreenColorTests covers the model and passes either way, so it cannot see
// this; only a rendered frame can.
//
// Self-skips without a GPU device, as eacp's own GPU suites do: renderToImage
// hands back an empty image there.

using namespace nano;
using namespace eacp;
using namespace term;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 160.f;

// Against the theme's own colour rather than a guess at what "red" looks like:
// ansi[1] is 0xf7768e, which is not red enough to pass a naive r>g,b test.
bool matches(const Graphics::Color& colour, Rgb expected)
{
    const auto target = Graphics::Color {
        (float) ((expected >> 16) & 0xFF) / 255.f,
        (float) ((expected >> 8) & 0xFF) / 255.f,
        (float) (expected & 0xFF) / 255.f,
        1.f};

    constexpr auto tolerance = 0.08f;

    return std::abs(colour.r - target.r) < tolerance
           && std::abs(colour.g - target.g) < tolerance
           && std::abs(colour.b - target.b) < tolerance;
}

int pixelsColoured(const Graphics::Image& image, Rgb expected)
{
    auto count = 0;

    for (auto y = 0; y < image.height(); ++y)
        for (auto x = 0; x < image.width(); ++x)
            if (matches(image.at(x, y), expected))
                ++count;

    return count;
}

// A pane running one command, its output parsed as it arrives. The trailing
// sleep keeps the shell up: the point is the frame, not the exit.
Graphics::Image renderAfter(const std::string& command)
{
    auto config = TerminalConfig {};
    auto view = TerminalView {config, "/tmp", {}, command + "; sleep 30"};

    // Also what builds the renderers -- they are made in resized().
    view.setBounds({0.f, 0.f, viewWidth, viewHeight});

    Threads::runEventLoopUntil(
        [&view] { return view.debugScreenText().find("HELLO") != std::string::npos; },
        Time::MS {8000});

    return view.renderToImage(1.f);
}
} // namespace

auto tAnsiBackgroundReachesThePixels =
    test("Render/aCellBackgroundReachesThePixels") = []
{
    // Five glyphs and five blank cells, all on red: the blanks are pure
    // background, so this cannot pass on glyph ink alone.
    const auto image = renderAfter("printf '\\033[41mHELLO     \\033[0m'");

    if (image.width() == 0)
        return;

    check(image.width() > 0 && image.height() > 0);
    check(pixelsColoured(image, Theme {}.ansi[1]) > 100,
          "the run's background has to be drawn, not just its glyphs");
};

auto tBackgroundSurvivesTheCursorPass =
    test("Render/aBackgroundSurvivesTheCursorPass") = []
{
    // The regression precisely: the cursor is drawn through a second
    // begin() on the same renderer, and begin() clears whatever is queued.
    // With the backgrounds left queued until then, the cursor pass threw the
    // whole frame's worth away and the terminal drew none at all.
    const auto image = renderAfter("printf '\\033[41mHELLO     \\033[0m'");

    if (image.width() == 0)
        return;

    check(pixelsColoured(image, Theme {}.ansi[1]) > 100,
          "drawing the cursor must not discard the backgrounds behind it");
};

auto tPlainTextHasNoBackground = test("Render/plainTextPaintsNoBackground") = []
{
    // The other direction, so the checks above cannot pass by painting red
    // over everything.
    const auto image = renderAfter("printf 'HELLO'");

    if (image.width() == 0)
        return;

    check(pixelsColoured(image, Theme {}.ansi[1]) == 0, "unstyled text paints no background");
};
