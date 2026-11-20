#pragma once

// The pane the view tests drive: a TerminalView over a recording fake shell,
// and the keys, mouse events and pixel checks they use.

#include <TerminalView/TerminalView.h>
#include <Emulator/TerminalModel.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace nano;
using namespace eacp;
using namespace term;

namespace
{
constexpr auto viewWidth = 420.f;
constexpr auto viewHeight = 160.f;
constexpr auto marginX = 6.f;
constexpr auto marginY = 4.f;
constexpr auto samplingInsetInPoints = 1.f;
constexpr auto colourTolerance = 0.08f;
constexpr auto outputTimeout = Time::MS {4000};

constexpr std::uint16_t macKeypadEnter = 0x4c;
constexpr std::uint16_t macForwardDelete = 0x75;
constexpr std::uint16_t macHome = 0x73;
constexpr std::uint16_t macEnd = 0x77;
constexpr std::uint16_t macPageUp = 0x74;
constexpr std::uint16_t macPageDown = 0x79;

struct ShellRecording
{
    std::string written;
    std::vector<PtySize> resizes;
    std::vector<PtySize> sizesTakenBack;
    std::function<void(std::optional<PtySize>)> setGridHeldElsewhere;
    std::function<void(const std::string&)> showStatus;
    std::function<void(std::string)> deliverOutput;
    std::function<void(const std::string&)> deliverRestoredState;
};

class RecordingShell final : public Shell
{
public:
    explicit RecordingShell(std::shared_ptr<ShellRecording> recordingToUse)
        : recording(std::move(recordingToUse))
    {
    }

    bool start(const PtyOptions&,
               std::function<void(std::string)> onOutput,
               std::function<void()>) override
    {
        recording->deliverOutput = std::move(onOutput);
        recording->deliverRestoredState = onRestoreState;
        recording->setGridHeldElsewhere = onGridSetElsewhere;
        recording->showStatus = onStatusChanged;
        return true;
    }

    void write(std::string_view data) override { recording->written += data; }
    void resize(const PtySize& size) override { recording->resizes.push_back(size); }
    std::string foregroundProcess() const override { return {}; }
    std::string currentWorkingDirectory() const override { return {}; }
    void terminate() override {}
    void detach() override {}
    void takeSizeBack(const PtySize& size) override { recording->sizesTakenBack.push_back(size); }

private:
    std::shared_ptr<ShellRecording> recording;
};

struct TestPane
{
    TestPane()
    {
        auto config = TerminalConfig {};
        config.makeShell = [this](const std::string&)
        { return std::make_unique<RecordingShell>(recording); };

        view = std::make_unique<TerminalView>(config, "/tmp");
        view->setBounds({0.f, 0.f, viewWidth, viewHeight});
        // Off-screen, setBounds alone never calls resized(), which builds the renderers.
        view->resized();
    }

    void showOutput(const std::string& bytes)
    {
        recording->deliverOutput(bytes);
        view->waitForParsedOutput();

        auto flushed = false;
        Threads::callAsync([&flushed] { flushed = true; });
        Threads::runEventLoopUntil([&flushed] { return flushed; }, outputTimeout);
    }

    std::string takeWritten()
    {
        auto written = std::string {};
        written.swap(recording->written);
        return written;
    }

    std::string writtenForKey(const Graphics::KeyEvent& event)
    {
        takeWritten();
        view->keyDown(event);
        return takeWritten();
    }

    std::shared_ptr<ShellRecording> recording = std::make_shared<ShellRecording>();
    std::unique_ptr<TerminalView> view;
};

inline Graphics::KeyEvent keyWithCode(std::uint16_t keyCode, Graphics::ModifierKeys mods = {})
{
    auto event = Graphics::KeyEvent {};
    event.keyCode = keyCode;
    event.modifiers = mods;
    return event;
}

inline Graphics::KeyEvent typedKey(const std::string& text, Graphics::ModifierKeys mods = {})
{
    auto event = Graphics::KeyEvent {};
    event.characters = text;
    event.charactersIgnoringModifiers = text;
    event.keyCode = 0xffff;
    event.modifiers = mods;
    return event;
}

inline Graphics::ModifierKeys shiftHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.shift = true;
    return mods;
}

inline Graphics::ModifierKeys controlHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.control = true;
    return mods;
}

inline Graphics::ModifierKeys altHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.alt = true;
    return mods;
}

inline Graphics::ModifierKeys commandHeld()
{
    auto mods = Graphics::ModifierKeys {};
    mods.command = true;
    return mods;
}

inline GlyphAtlas& referenceAtlas()
{
    static auto atlas = GlyphAtlas {TerminalConfig {}.font, TerminalConfig {}.fontSize};
    return atlas;
}

inline Graphics::Point centreOfCell(int col, int row)
{
    const auto& atlas = referenceAtlas();
    return {marginX + ((float) col + 0.5f) * atlas.cellWidth(),
            marginY + ((float) row + 0.5f) * atlas.cellHeight()};
}

inline Graphics::MouseEvent mouseAtCell(int col, int row)
{
    auto event = Graphics::MouseEvent {};
    event.pos = centreOfCell(col, row);
    return event;
}

inline Graphics::MouseEvent shiftMouseAtCell(int col, int row)
{
    auto event = mouseAtCell(col, row);
    event.modifiers = shiftHeld();
    return event;
}

inline Graphics::MouseEvent wheelBy(float lines)
{
    auto event = mouseAtCell(0, 0);
    event.delta = {0.f, lines};
    return event;
}

inline Graphics::MouseEvent trackpadScrollBy(float lines)
{
    auto event = mouseAtCell(0, 0);
    event.delta = {0.f, lines * referenceAtlas().cellHeight()};
    event.preciseScrolling = true;
    return event;
}

inline bool matches(const Graphics::Color& colour, Rgb expected)
{
    const auto target = toColor(expected);

    return std::abs(colour.r - target.r) < colourTolerance
           && std::abs(colour.g - target.g) < colourTolerance
           && std::abs(colour.b - target.b) < colourTolerance;
}

// The colour just inside a cell's top-left corner, where lowercase letters
// leave the background showing.
inline bool isCellPainted(const Graphics::Image& image, int col, int row, Rgb expected)
{
    const auto& atlas = referenceAtlas();
    const auto x = marginX + (float) col * atlas.cellWidth() + samplingInsetInPoints;
    const auto y = marginY + (float) row * atlas.cellHeight() + samplingInsetInPoints;
    return matches(image.at((int) std::ceil(x), (int) std::ceil(y)), expected);
}

const Theme defaultTheme {};
} // namespace
