#include "SelectGraphicRendition.h"

#include <algorithm>
#include <array>
#include <optional>

namespace term
{
namespace
{
constexpr auto resetCode = 0;
constexpr auto underlineCode = 4;
constexpr auto extendedForegroundCode = 38;
constexpr auto defaultForegroundCode = 39;
constexpr auto extendedBackgroundCode = 48;
constexpr auto defaultBackgroundCode = 49;

constexpr auto underlineStyleNone = 0;
constexpr auto indexedColorKind = 5;
constexpr auto directColorKind = 2;
constexpr auto largestColorComponent = 255;
constexpr auto redShift = 16;
constexpr auto greenShift = 8;

struct AttributeChange
{
    int code;
    std::uint16_t set;
    std::uint16_t clear;
};

constexpr std::array<AttributeChange, 16> attributeChanges = {{
    {1, Attr::Bold, 0},
    {2, Attr::Faint, 0},
    {3, Attr::Italic, 0},
    {5, Attr::Blink, 0},
    {6, Attr::Blink, 0},
    {7, Attr::Inverse, 0},
    {8, Attr::Hidden, 0},
    {9, Attr::Strike, 0},
    {21, 0, Attr::Bold | Attr::Faint},
    {22, 0, Attr::Bold | Attr::Faint},
    {23, 0, Attr::Italic},
    {24, 0, Attr::Underline},
    {25, 0, Attr::Blink},
    {27, 0, Attr::Inverse},
    {28, 0, Attr::Hidden},
    {29, 0, Attr::Strike},
}};

struct PaletteColorRange
{
    int firstCode;
    int lastCode;
    int firstPaletteIndex;
    bool isForeground;
};

constexpr std::array<PaletteColorRange, 4> paletteColorRanges = {{
    {30, 37, 0, true},
    {40, 47, 0, false},
    {90, 97, 8, true},
    {100, 107, 8, false},
}};

struct ExtendedColor
{
    std::optional<Rgb> color;
    std::size_t extraGroupsConsumed = 0;
};

Rgb packColor(int red, int green, int blue)
{
    const auto clampComponent = [](int value)
    { return (Rgb) std::clamp(value, 0, largestColorComponent); };

    return (clampComponent(red) << redShift) | (clampComponent(green) << greenShift)
           | clampComponent(blue);
}

// Colon form carries the spec inside one group: 38:5:n or 38:2[:id]:r:g:b.
std::optional<Rgb> readColonSeparatedColor(const CsiParameters::Group& group, const Theme& theme)
{
    constexpr auto indexedGroupSize = std::size_t {3};
    constexpr auto directGroupSize = std::size_t {5};
    constexpr auto directGroupSizeWithColorSpace = std::size_t {6};

    if (group[1] == indexedColorKind && group.size() >= indexedGroupSize)
        return theme.indexed(group[2]);

    if (group[1] != directColorKind || group.size() < directGroupSize)
        return std::nullopt;

    const auto first = group.size() >= directGroupSizeWithColorSpace ? std::size_t {3} : std::size_t {2};
    return packColor(group[first], group[first + 1], group[first + 2]);
}

// Semicolon form spreads across groups: 38;5;n or 38;2;r;g;b.
ExtendedColor readSemicolonSeparatedColor(const CsiParameters& parameters,
                                          std::size_t index,
                                          const Theme& theme)
{
    constexpr auto indexedExtraGroups = std::size_t {2};
    constexpr auto directExtraGroups = std::size_t {4};

    const auto kind = parameters.value(index + 1, -1);

    if (kind == indexedColorKind && index + indexedExtraGroups < parameters.size())
        return {theme.indexed(parameters.value(index + 2, 0)), indexedExtraGroups};

    if (kind == directColorKind && index + directExtraGroups < parameters.size())
    {
        const auto color = packColor(parameters.value(index + 2, 0),
                                     parameters.value(index + 3, 0),
                                     parameters.value(index + 4, 0));
        return {color, directExtraGroups};
    }

    return {};
}

ExtendedColor readExtendedColor(const CsiParameters& parameters, std::size_t index, const Theme& theme)
{
    const auto& group = parameters.group(index);

    if (group.size() >= 2)
        return {readColonSeparatedColor(group, theme), 0};

    if (index + 1 >= parameters.size())
        return {};

    return readSemicolonSeparatedColor(parameters, index, theme);
}

void setForeground(Pen& pen, Rgb color)
{
    pen.fg = color;
    pen.defaultFg = false;
}

void setBackground(Pen& pen, Rgb color)
{
    pen.bg = color;
    pen.defaultBg = false;
}

void resetPen(Pen& pen, const Theme& theme)
{
    pen.attrs = 0;
    pen.defaultFg = true;
    pen.defaultBg = true;
    pen.fg = theme.foreground;
    pen.bg = theme.background;
}

bool applyAttributeChange(Pen& pen, int code)
{
    const auto change = std::find_if(attributeChanges.begin(),
                                     attributeChanges.end(),
                                     [code](const AttributeChange& entry) { return entry.code == code; });

    if (change == attributeChanges.end())
        return false;

    pen.attrs = (std::uint16_t) ((pen.attrs | change->set) & ~change->clear);
    return true;
}

void applyPaletteColor(Pen& pen, int code, const Theme& theme)
{
    for (const auto& range: paletteColorRanges)
    {
        if (code < range.firstCode || code > range.lastCode)
            continue;

        const auto color = theme.ansi[(std::size_t) (code - range.firstCode + range.firstPaletteIndex)];

        if (range.isForeground)
            setForeground(pen, color);
        else
            setBackground(pen, color);

        return;
    }
}

void applyUnderline(Pen& pen, const CsiParameters::Group& group)
{
    if (group.size() >= 2 && group[1] == underlineStyleNone)
        pen.attrs &= (std::uint16_t) ~Attr::Underline;
    else
        pen.attrs |= Attr::Underline;
}

std::size_t applyExtendedColor(Pen& pen,
                               const CsiParameters& parameters,
                               std::size_t index,
                               const Theme& theme)
{
    const auto extended = readExtendedColor(parameters, index, theme);

    if (!extended.color)
        return extended.extraGroupsConsumed;

    if (parameters.value(index, 0) == extendedForegroundCode)
        setForeground(pen, *extended.color);
    else
        setBackground(pen, *extended.color);

    return extended.extraGroupsConsumed;
}

void applyDefaultColor(Pen& pen, int code, const Theme& theme)
{
    if (code == defaultForegroundCode)
    {
        pen.defaultFg = true;
        pen.fg = theme.foreground;
        return;
    }

    pen.defaultBg = true;
    pen.bg = theme.background;
}

// Returns how many groups after `index` the parameter used up.
std::size_t applyParameter(Pen& pen, const CsiParameters& parameters, std::size_t index, const Theme& theme)
{
    const auto code = parameters.value(index, resetCode);

    if (code == extendedForegroundCode || code == extendedBackgroundCode)
        return applyExtendedColor(pen, parameters, index, theme);

    if (code == defaultForegroundCode || code == defaultBackgroundCode)
        applyDefaultColor(pen, code, theme);
    else if (code == resetCode)
        resetPen(pen, theme);
    else if (code == underlineCode)
        applyUnderline(pen, parameters.group(index));
    else if (!applyAttributeChange(pen, code))
        applyPaletteColor(pen, code, theme);

    return 0;
}
} // namespace

void applySelectGraphicRendition(Pen& pen, const CsiParameters& parameters, const Theme& theme)
{
    for (std::size_t index = 0; index < parameters.size(); ++index)
        index += applyParameter(pen, parameters, index, theme);
}
} // namespace term
