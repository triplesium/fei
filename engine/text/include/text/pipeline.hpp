#pragma once

#include "asset/assets.hpp"
#include "asset/id.hpp"
#include "core/image.hpp"
#include "math/vector.hpp"
#include "text/font.hpp"
#include "text/text.hpp"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace fei::text {

class TextPipeline {
  private:
    struct GlyphKey {
        AssetId font {invalid_asset_id};
        int32 glyph {0};
        uint32 size {0};

        bool operator==(const GlyphKey&) const = default;
    };

    struct GlyphKeyHash {
        std::size_t operator()(const GlyphKey& key) const;
    };

    struct AtlasGlyph {
        Rect uv;
        Handle<Image> atlas;
    };

    struct Atlas {
        Handle<Image> image;
        std::vector<uint8> pixels;
        uint32 cursor_x {1};
        uint32 cursor_y {1};
        uint32 row_height {0};
    };

    std::vector<Atlas> m_atlases;
    std::unordered_map<GlyphKey, AtlasGlyph, GlyphKeyHash> m_glyphs;

    AtlasGlyph cache_glyph(
        AssetId font_id,
        const Font& font,
        int32 glyph_id,
        float font_size,
        Assets<Image>& images
    );

  public:
    [[nodiscard]] TextMeasureInfo create_measure(
        const Font& font,
        std::string_view value,
        float font_size
    ) const;

    [[nodiscard]] Vector2
    measure(const Font& font, std::string_view value, float font_size) const;

    void layout(
        AssetId font_id,
        const Font& font,
        const TextMeasureInfo& measure,
        float font_size,
        const TextLayout& text_layout,
        Vector2 bounds,
        Assets<Image>& images,
        TextLayoutInfo& output
    );
};

} // namespace fei::text
