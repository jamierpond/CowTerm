#include "GlyphAtlas.h"

#include <ResEmbed/ResEmbed.h>

#include <algorithm>
#include <cmath>

namespace term
{
using namespace eacp;

void registerEmbeddedFonts()
{
    static const auto once = []
    {
        for (const auto* name: {"JetBrainsMono-Regular.ttf",
                                "JetBrainsMono-Bold.ttf",
                                "JetBrainsMono-Italic.ttf",
                                "JetBrainsMono-BoldItalic.ttf"})
        {
            const auto resource = ResEmbed::get(name);

            if (resource.size() == 0)
                continue;

            Text::registerMemoryFont(resource.data(), resource.size());
        }

        return true;
    }();

    (void) once;
}

GlyphAtlas::GlyphAtlas(const std::string& fontName, float size, float scaleToUse)
    : pointSize(size)
{
    auto request = Text::FontRequest {};
    request.family = fontName;
    request.pointSize = size;
    request.scale = scaleToUse > 0 ? scaleToUse : 2.f;

    backingScale = request.scale;

    auto rasterizer = makeOwned<Text::GlyphRasterizer>(request);

    // 1024 rather than the old fixed 2048: it grows on demand now, so starting
    // smaller costs nothing and a session that only ever shows ASCII never
    // allocates more.
    impl = makeOwned<Text::GlyphAtlas>(
        OwningPointer<Text::GlyphSource> {std::move(rasterizer)}, 1024, 4096);

    const auto metrics = impl->metrics();

    // The cell is the face's own advance and line height, which is what the
    // rasterizer already reports — the previous version measured 'M' by hand
    // to get the same number.
    cellW = std::max(std::ceil(metrics.advance), 1.f);
    cellH = std::max(std::ceil(metrics.lineHeight()), 1.f);
    ascent = metrics.ascent;
}

GlyphSlot GlyphAtlas::glyph(char32_t codepoint, bool bold, bool italic, int cells)
{
    const auto style = Text::toFontStyle(bold, italic);
    const auto glyph = impl->glyph(codepoint, style);

    auto slot = GlyphSlot {};
    slot.valid = glyph.valid;
    slot.empty = glyph.empty;
    slot.colored = glyph.format == Text::GlyphFormat::Color;
    slot.src = glyph.src;

    if (!glyph.valid || glyph.empty)
        return slot;

    // Centre the glyph in its cell span. A monospace face's own advance already
    // matches the cell, so this is a no-op for ordinary text; it matters for
    // glyphs that came from a fallback face — CJK and emoji — whose advance
    // does not line up with two cells.
    const auto span = static_cast<float>(std::max(cells, 1)) * cellW;
    const auto width = glyph.src.w / backingScale;
    const auto centring = std::max((span - width) * 0.5f, 0.f);

    slot.offset = {centring + glyph.offset.x, glyph.offset.y};

    return slot;
}

void GlyphAtlas::commit()
{
    impl->commit();
}
} // namespace term
