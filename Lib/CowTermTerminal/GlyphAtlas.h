#pragma once

#include <eacp/Text/Text.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace term
{
// Registers the embedded fonts (JetBrains Mono) with the platform font
// system so name lookups resolve them. Idempotent; call before any Font or
// GlyphAtlas is created.
void registerEmbeddedFonts();

// Where a glyph lives in the atlas and how to place it in a terminal cell.
struct GlyphSlot
{
    // Texel rect in the atlas texture of this slot's format.
    eacp::Graphics::Rect src;

    // Offset in points from the cell's left edge and baseline to the bitmap's
    // top-left, including the horizontal centring a fixed grid wants.
    eacp::Graphics::Point offset;

    bool colored = false;
    bool valid = false;

    // Valid but with nothing to draw — a space. The cell still advances.
    bool empty = false;
};

// A monospace cell grid over eacp's glyph atlas.
//
// This was ~870 lines: a CoreText rasterizer and a DirectWrite one, each
// carrying its own copy of the packing, blitting, eviction and upload logic.
// That all lives in eacp-text now, portable above a much narrower platform
// seam, so what is left here is the part that is genuinely CowTerm's — turning
// a variable-width glyph into a fixed cell.
//
// Behaviour that improves as a side effect of the move:
//
//  - The mask atlas is R8Unorm rather than RGBA8, a quarter of the memory.
//  - A new glyph uploads only its own region instead of re-sending the entire
//    atlas, which was 16 MB per newly seen character.
//  - The atlas grows when it fills rather than being flushed wholesale, so a
//    long session no longer periodically re-rasterizes everything it knows.
//  - Glyphs carry real bearings and sit on a shared baseline, instead of each
//    being centred in its own cell.
class GlyphAtlas
{
public:
    // scale <= 0 uses the main display's backing scale.
    GlyphAtlas(const std::string& fontName, float pointSize, float scale = 0);

    // All in points.
    float cellWidth() const { return cellW; }
    float cellHeight() const { return cellH; }
    float baseline() const { return ascent; }
    float fontSize() const { return pointSize; }

    // `cells` is 1 or 2; a double-width glyph is centred across both.
    GlyphSlot glyph(char32_t codepoint, bool bold, bool italic, int cells = 1);

    // A grapheme cluster (ZWJ emoji, a letter with combining marks) shaped as
    // one string and centred across `cells`. Falls back to the first
    // codepoint's glyph when the font can't shape every part.
    std::vector<GlyphSlot> clusterGlyphs(std::u32string_view cluster, bool bold, bool italic, int cells);

    // Uploads whatever was rasterized since the last call. Call once per frame,
    // after every glyph the frame needs has been requested and before the first
    // draw that samples the atlas — uploading mid-pass would mutate a texture
    // the earlier draws have already bound.
    void commit();

    eacp::Text::GlyphAtlas& atlas() { return *impl; }
    float scale() const { return backingScale; }

    // Changes when the atlas resets and every slot handed out goes stale.
    std::uint32_t generation() const { return impl->generation(); }

private:
    eacp::OwningPointer<eacp::Text::GlyphAtlas> impl;

    float pointSize = 13.f;
    float backingScale = 2.f;
    float cellW = 0.f;
    float cellH = 0.f;
    float ascent = 0.f;
};
} // namespace term
