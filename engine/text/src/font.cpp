#include "text/font.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace fei::text {

struct Font::Impl {
    std::vector<uint8> bytes;
    stbtt_fontinfo info {};
};

Font::Font(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

Font::Font(Font&&) noexcept = default;
Font& Font::operator=(Font&&) noexcept = default;
Font::~Font() = default;

Result<std::unique_ptr<Font>, std::string>
Font::from_bytes(std::span<const std::byte> bytes) {
    if (bytes.empty() ||
        bytes.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return failure(std::string("Font data is empty or too large"));
    }

    auto impl = std::make_unique<Impl>();
    impl->bytes.resize(bytes.size());
    std::memcpy(impl->bytes.data(), bytes.data(), bytes.size());
    const auto offset = stbtt_GetFontOffsetForIndex(impl->bytes.data(), 0);
    if (offset < 0 ||
        !stbtt_InitFont(&impl->info, impl->bytes.data(), offset)) {
        return failure(std::string("Invalid TrueType or OpenType font"));
    }
    return std::unique_ptr<Font>(new Font(std::move(impl)));
}

int32 Font::glyph_id(char32_t codepoint) const {
    return stbtt_FindGlyphIndex(&m_impl->info, static_cast<int>(codepoint));
}

FontMetrics Font::metrics(float font_size) const {
    if (font_size <= 0.0f) {
        return {};
    }
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&m_impl->info, &ascent, &descent, &line_gap);
    const auto scale = stbtt_ScaleForPixelHeight(&m_impl->info, font_size);
    return {
        .ascent = static_cast<float>(ascent) * scale,
        .descent = static_cast<float>(descent) * scale,
        .line_gap = static_cast<float>(line_gap) * scale,
    };
}

float Font::advance(int32 glyph_id, float font_size) const {
    if (font_size <= 0.0f) {
        return 0.0f;
    }
    int advance = 0;
    int bearing = 0;
    stbtt_GetGlyphHMetrics(&m_impl->info, glyph_id, &advance, &bearing);
    (void)bearing;
    return static_cast<float>(advance) *
           stbtt_ScaleForPixelHeight(&m_impl->info, font_size);
}

float Font::kerning(
    int32 left_glyph,
    int32 right_glyph,
    float font_size
) const {
    if (font_size <= 0.0f) {
        return 0.0f;
    }
    return static_cast<float>(
               stbtt_GetGlyphKernAdvance(&m_impl->info, left_glyph, right_glyph)
           ) *
           stbtt_ScaleForPixelHeight(&m_impl->info, font_size);
}

RasterizedGlyph Font::rasterize(int32 glyph_id, float font_size) const {
    RasterizedGlyph glyph {.glyph_id = glyph_id};
    if (font_size <= 0.0f) {
        return glyph;
    }
    const auto scale = stbtt_ScaleForPixelHeight(&m_impl->info, font_size);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(
        &m_impl->info,
        glyph_id,
        scale,
        scale,
        &x0,
        &y0,
        &x1,
        &y1
    );
    glyph.offset_x = x0;
    glyph.offset_y = y0;
    glyph.width = static_cast<uint32>(std::max(0, x1 - x0));
    glyph.height = static_cast<uint32>(std::max(0, y1 - y0));
    glyph.pixels.resize(static_cast<std::size_t>(glyph.width) * glyph.height);
    if (!glyph.pixels.empty()) {
        stbtt_MakeGlyphBitmap(
            &m_impl->info,
            glyph.pixels.data(),
            static_cast<int>(glyph.width),
            static_cast<int>(glyph.height),
            static_cast<int>(glyph.width),
            scale,
            scale,
            glyph_id
        );
    }
    return glyph;
}

AssetLoadResult<Font>
FontLoader::load(Reader& reader, const LoadContext& context) {
    auto font = Font::from_bytes(std::span(reader.data(), reader.size()));
    if (!font) {
        return failure(AssetLoadError(context.asset_path(), font.error()));
    }
    return std::move(*font);
}

} // namespace fei::text
