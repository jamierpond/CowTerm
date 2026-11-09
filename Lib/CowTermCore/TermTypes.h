#pragma once

#include <eacp/Graphics/Primitives/Primitives.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>

namespace term
{
// Packed 0xRRGGBB.
using Rgb = std::uint32_t;

// A resolved cell's attribute bits. The Default* bits mark colours that
// follow the theme's default fg/bg so inverse video stays faithful; Wide,
// WideCont and WrapSpacer come from the cell itself, not its style.
namespace Attr
{
constexpr std::uint16_t Bold = 1 << 0;
constexpr std::uint16_t Faint = 1 << 1;
constexpr std::uint16_t Italic = 1 << 2;
constexpr std::uint16_t Underline = 1 << 3;
constexpr std::uint16_t Blink = 1 << 4;
constexpr std::uint16_t Inverse = 1 << 5;
constexpr std::uint16_t Hidden = 1 << 6;
constexpr std::uint16_t Strike = 1 << 7;
constexpr std::uint16_t Wide = 1 << 8;
constexpr std::uint16_t WideCont = 1 << 9;
constexpr std::uint16_t DefaultFg = 1 << 10;
constexpr std::uint16_t DefaultBg = 1 << 11;

// The blank left in a row's last column when a wide character didn't fit
// and wrapped early; rewrapping drops it.
constexpr std::uint16_t WrapSpacer = 1 << 12;
} // namespace Attr

// Colours and attributes, stored once per screen and shared by every cell
// that uses them (Ghostty's style set).
struct CellStyle
{
    Rgb fg = 0;
    Rgb bg = 0;
    std::uint16_t attrs = Attr::DefaultFg | Attr::DefaultBg;

    bool operator==(const CellStyle&) const = default;
};

struct CellStyleHash
{
    std::size_t operator()(const CellStyle& style) const
    {
        const auto colours = ((std::uint64_t) style.fg << 32) | style.bg;
        return std::hash<std::uint64_t> {}(colours) ^ ((std::size_t) style.attrs << 1);
    }
};

using StyleId = std::uint16_t;

// What a cell holds besides its style and link, in content's top bits.
namespace CellFlag
{
constexpr std::uint32_t Wide = 1u << 21;
constexpr std::uint32_t WideCont = 1u << 22;
constexpr std::uint32_t WrapSpacer = 1u << 23;

// content's low bits are a grapheme cluster's id, not a codepoint.
constexpr std::uint32_t Grapheme = 1u << 24;
} // namespace CellFlag

// One grid cell in 8 bytes: a codepoint (or a grapheme cluster's id) and
// flags, a style id and a hyperlink id. The screen resolves the ids.
struct Cell
{
    static constexpr std::uint32_t valueMask = (1u << 21) - 1;

    std::uint32_t content = U' ';
    StyleId style = 0;
    std::uint16_t hyperlink = 0;

    std::uint32_t value() const { return content & valueMask; }
    bool hasFlag(std::uint32_t flag) const { return (content & flag) != 0; }
    void setFlag(std::uint32_t flag, bool on) { content = on ? content | flag : content & ~flag; }
    bool isGrapheme() const { return hasFlag(CellFlag::Grapheme); }

    // The single codepoint, or 0 for a grapheme cluster.
    char32_t codepoint() const { return isGrapheme() ? 0 : (char32_t) value(); }
    bool isSpace() const { return !isGrapheme() && value() == U' '; }

    void setCodepoint(char32_t codepoint)
    {
        content = (content & ~(valueMask | CellFlag::Grapheme)) | ((std::uint32_t) codepoint & valueMask);
    }
};

// A cell as the renderer and text readers see it: its whole character, its
// colours and every attribute bit.
struct ResolvedCell
{
    // The first codepoint; text is the whole cluster (one codepoint or more).
    char32_t ch = U' ';
    std::u32string_view text;
    Rgb fg = 0;
    Rgb bg = 0;
    std::uint16_t attrs = Attr::DefaultFg | Attr::DefaultBg;
    std::uint16_t hyperlink = 0;
};

inline eacp::Graphics::Color toColor(Rgb rgb, float alpha = 1.0f)
{
    return {(float) ((rgb >> 16) & 0xff) / 255.0f,
            (float) ((rgb >> 8) & 0xff) / 255.0f,
            (float) (rgb & 0xff) / 255.0f,
            alpha};
}

struct Theme
{
    Rgb background = 0x1a1b26;
    Rgb foreground = 0xc0caf5;
    Rgb cursor = 0xc0caf5;
    Rgb selection = 0x33467c;
    Rgb paneBorder = 0x1f2335;
    Rgb paneBorderActive = 0x3b4261;

    std::array<Rgb, 16> ansi = {0x15161e,
                                0xf7768e,
                                0x9ece6a,
                                0xe0af68,
                                0x7aa2f7,
                                0xbb9af7,
                                0x7dcfff,
                                0xa9b1d6,
                                0x414868,
                                0xf7768e,
                                0x9ece6a,
                                0xe0af68,
                                0x7aa2f7,
                                0xbb9af7,
                                0x7dcfff,
                                0xc0caf5};

    Rgb indexed(int index) const
    {
        if (index < 0)
            return foreground;

        if (index < 16)
            return ansi[(std::size_t) index];

        if (index < 232)
        {
            constexpr std::array<Rgb, 6> steps = {0, 95, 135, 175, 215, 255};
            const auto n = index - 16;
            const auto r = steps[(std::size_t) (n / 36)];
            const auto g = steps[(std::size_t) ((n / 6) % 6)];
            const auto b = steps[(std::size_t) (n % 6)];
            return (r << 16) | (g << 8) | b;
        }

        if (index < 256)
        {
            const auto v = (Rgb) (8 + 10 * (index - 232));
            return (v << 16) | (v << 8) | v;
        }

        return foreground;
    }
};

// East-Asian/emoji width classes the renderer and screen model agree on:
// 0 = combining/zero-width, 2 = double cell, 1 = everything else.
int charWidth(char32_t cp);
} // namespace term
