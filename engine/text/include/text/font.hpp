#pragma once

#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "base/result.hpp"
#include "base/types.hpp"
#include "refl/reflect.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace fei::text {

struct FontMetrics {
    float ascent {0.0f};
    float descent {0.0f};
    float line_gap {0.0f};
};

struct RasterizedGlyph {
    int32 glyph_id {0};
    int32 offset_x {0};
    int32 offset_y {0};
    uint32 width {0};
    uint32 height {0};
    std::vector<uint8> pixels;
};

FEI_REFLECT()
class Font {
  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    explicit Font(std::unique_ptr<Impl> impl);

  public:
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    ~Font();

    [[nodiscard]] static Result<std::unique_ptr<Font>, std::string>
    from_bytes(std::span<const std::byte> bytes);

    [[nodiscard]] int32 glyph_id(char32_t codepoint) const;
    [[nodiscard]] FontMetrics metrics(float font_size) const;
    [[nodiscard]] float advance(int32 glyph_id, float font_size) const;
    [[nodiscard]] float
    kerning(int32 left_glyph, int32 right_glyph, float font_size) const;
    [[nodiscard]] RasterizedGlyph
    rasterize(int32 glyph_id, float font_size) const;
};

class FontLoader : public AssetLoader<Font> {
  public:
    AssetLoadResult<Font>
    load(Reader& reader, const LoadContext& context) override;
};

} // namespace fei::text
