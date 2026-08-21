#include "text/pipeline.hpp"

#include "graphics/enums.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace fei::text {

namespace {

constexpr uint32 atlas_size = 512;
constexpr uint32 atlas_padding = 1;

std::vector<char32_t> decode_utf8(std::string_view value) {
    std::vector<char32_t> output;
    output.reserve(value.size());
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<uint8>(value[index]);
        char32_t codepoint = 0xfffd;
        std::size_t count = 1;
        if (first < 0x80U) {
            codepoint = first;
        } else if ((first & 0xe0U) == 0xc0U) {
            codepoint = first & 0x1fU;
            count = 2;
        } else if ((first & 0xf0U) == 0xe0U) {
            codepoint = first & 0x0fU;
            count = 3;
        } else if ((first & 0xf8U) == 0xf0U) {
            codepoint = first & 0x07U;
            count = 4;
        }
        if (index + count > value.size()) {
            count = 1;
            codepoint = 0xfffd;
        } else if (count > 1) {
            for (std::size_t byte = 1; byte < count; ++byte) {
                const auto continuation =
                    static_cast<uint8>(value[index + byte]);
                if ((continuation & 0xc0U) != 0x80U) {
                    count = 1;
                    codepoint = 0xfffd;
                    break;
                }
                codepoint = (codepoint << 6U) | (continuation & 0x3fU);
            }
        }
        output.push_back(codepoint);
        index += count;
    }
    return output;
}

uint32 quantize_size(float font_size) {
    return static_cast<uint32>(std::max(0L, std::lround(font_size * 64.0f)));
}

float range_width(
    const std::vector<MeasuredGlyph>& glyphs,
    std::size_t begin,
    std::size_t end
) {
    float width = 0.0f;
    for (auto index = begin; index < end; ++index) {
        if (index > begin) {
            width += glyphs[index].kerning;
        }
        width += glyphs[index].advance;
    }
    return width;
}

} // namespace

std::vector<TextLine>
TextMeasureInfo::lines(Optional<float> max_width, LineBreak line_break) const {
    std::vector<TextLine> result;
    const auto append = [&](std::size_t begin, std::size_t end) {
        while (end > begin && glyphs[end - 1].whitespace) {
            --end;
        }
        result.push_back(
            TextLine {
                .begin = begin,
                .end = end,
                .width = range_width(glyphs, begin, end),
            }
        );
    };
    const auto wrap = [&](std::size_t paragraph_begin,
                          std::size_t paragraph_end) {
        if (paragraph_begin == paragraph_end) {
            append(paragraph_begin, paragraph_end);
            return;
        }
        if (!max_width || line_break == LineBreak::NoWrap) {
            append(paragraph_begin, paragraph_end);
            return;
        }

        auto line_begin = paragraph_begin;
        while (line_begin < paragraph_end) {
            while (line_begin < paragraph_end &&
                   glyphs[line_begin].whitespace) {
                ++line_begin;
            }
            if (line_begin == paragraph_end) {
                append(line_begin, line_begin);
                break;
            }

            float width = 0.0f;
            auto cursor = line_begin;
            auto last_break = paragraph_end;
            bool overflowed = false;
            for (; cursor < paragraph_end; ++cursor) {
                if (glyphs[cursor].whitespace) {
                    last_break = cursor;
                }
                const auto next_width =
                    width +
                    (cursor > line_begin ? glyphs[cursor].kerning : 0.0f) +
                    glyphs[cursor].advance;
                if (cursor > line_begin && next_width > *max_width) {
                    overflowed = true;
                    break;
                }
                width = next_width;
            }
            if (!overflowed) {
                append(line_begin, paragraph_end);
                break;
            }

            auto line_end = cursor;
            if (line_break != LineBreak::AnyCharacter &&
                last_break != paragraph_end && last_break > line_begin) {
                line_end = last_break;
            } else if (line_break == LineBreak::WordBoundary) {
                auto word_end = cursor;
                while (word_end < paragraph_end &&
                       !glyphs[word_end].whitespace) {
                    ++word_end;
                }
                line_end = word_end;
            }
            line_end = std::max(line_end, line_begin + 1);
            append(line_begin, line_end);
            line_begin = line_end;
            while (line_begin < paragraph_end &&
                   glyphs[line_begin].whitespace) {
                ++line_begin;
            }
        }
    };

    std::size_t paragraph_begin = 0;
    for (std::size_t index = 0; index <= glyphs.size(); ++index) {
        if (index == glyphs.size() || glyphs[index].codepoint == U'\n') {
            wrap(paragraph_begin, index);
            paragraph_begin = index + 1;
        }
    }
    return result;
}

Vector2 TextMeasureInfo::compute_size(
    Optional<float> max_width,
    LineBreak line_break
) const {
    if (glyphs.empty()) {
        return Vector2::Zero;
    }
    const auto measured_lines = lines(max_width, line_break);
    float width = 0.0f;
    for (const auto& line : measured_lines) {
        width = std::max(width, line.width);
    }
    return {width, line_height * static_cast<float>(measured_lines.size())};
}

std::size_t TextPipeline::GlyphKeyHash::operator()(const GlyphKey& key) const {
    auto seed = std::hash<AssetId> {}(key.font);
    seed ^= std::hash<int32> {}(key.glyph) + 0x9e3779b9U + (seed << 6U) +
            (seed >> 2U);
    seed ^= std::hash<uint32> {}(key.size) + 0x9e3779b9U + (seed << 6U) +
            (seed >> 2U);
    return seed;
}

TextMeasureInfo TextPipeline::create_measure(
    const Font& font,
    std::string_view value,
    float font_size
) const {
    if (font_size <= 0.0f || value.empty()) {
        return {};
    }
    const auto metrics = font.metrics(font_size);
    TextMeasureInfo result {
        .ascent = metrics.ascent,
        .line_height = metrics.ascent - metrics.descent + metrics.line_gap,
    };
    const auto codepoints = decode_utf8(value);
    result.glyphs.reserve(codepoints.size());
    int32 previous = -1;
    for (const auto codepoint : codepoints) {
        if (codepoint == U'\n') {
            result.glyphs.push_back(MeasuredGlyph {.codepoint = codepoint});
            previous = -1;
            continue;
        }
        const auto glyph = font.glyph_id(codepoint);
        result.glyphs.push_back(
            MeasuredGlyph {
                .codepoint = codepoint,
                .glyph_id = glyph,
                .advance = font.advance(glyph, font_size),
                .kerning = previous >= 0 ?
                               font.kerning(previous, glyph, font_size) :
                               0.0f,
                .whitespace = codepoint == U' ' || codepoint == U'\t',
            }
        );
        previous = glyph;
    }
    result.max = result.compute_size(nullopt, LineBreak::NoWrap);
    float minimum_width = 0.0f;
    float word_width = 0.0f;
    bool word_started = false;
    for (const auto& glyph : result.glyphs) {
        if (glyph.whitespace || glyph.codepoint == U'\n') {
            minimum_width = std::max(minimum_width, word_width);
            word_width = 0.0f;
            word_started = false;
            continue;
        }
        if (word_started) {
            word_width += glyph.kerning;
        }
        word_width += glyph.advance;
        word_started = true;
    }
    minimum_width = std::max(minimum_width, word_width);
    result.min = {minimum_width, result.line_height};
    return result;
}

Vector2 TextPipeline::measure(
    const Font& font,
    std::string_view value,
    float font_size
) const {
    return create_measure(font, value, font_size).max;
}

TextPipeline::AtlasGlyph TextPipeline::cache_glyph(
    AssetId font_id,
    const Font& font,
    int32 glyph_id,
    float font_size,
    Assets<Image>& images
) {
    const GlyphKey key {
        .font = font_id,
        .glyph = glyph_id,
        .size = quantize_size(font_size),
    };
    if (const auto item = m_glyphs.find(key); item != m_glyphs.end()) {
        return item->second;
    }

    const auto glyph = font.rasterize(glyph_id, font_size);
    if (glyph.width == 0 || glyph.height == 0 ||
        glyph.width + atlas_padding * 2 > atlas_size ||
        glyph.height + atlas_padding * 2 > atlas_size) {
        return {};
    }

    Atlas* selected = nullptr;
    uint32 selected_x = 0;
    uint32 selected_y = 0;
    for (auto& atlas : m_atlases) {
        auto x = atlas.cursor_x;
        auto y = atlas.cursor_y;
        if (x + glyph.width + atlas_padding > atlas_size) {
            x = atlas_padding;
            y += atlas.row_height;
        }
        if (y + glyph.height + atlas_padding <= atlas_size) {
            selected = &atlas;
            selected_x = x;
            selected_y = y;
            break;
        }
    }
    if (!selected) {
        auto image = Image::create_empty(
            atlas_size,
            atlas_size,
            1,
            PixelFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            TextureType::Texture2D
        );
        auto handle = images.add(std::move(image));
        m_atlases.push_back(
            Atlas {
                .image = handle,
                .pixels = std::vector<uint8>(
                    static_cast<std::size_t>(atlas_size) * atlas_size
                ),
            }
        );
        selected = &m_atlases.back();
        selected_x = atlas_padding;
        selected_y = atlas_padding;
    }

    if (selected_x == atlas_padding && selected->cursor_x != atlas_padding) {
        selected->cursor_y += selected->row_height;
        selected->cursor_x = atlas_padding;
        selected->row_height = 0;
        selected_y = selected->cursor_y;
    }
    for (uint32 row = 0; row < glyph.height; ++row) {
        const auto source =
            glyph.pixels.data() + static_cast<std::size_t>(row) * glyph.width;
        auto* destination =
            selected->pixels.data() +
            static_cast<std::size_t>(selected_y + row) * atlas_size +
            selected_x;
        std::memcpy(destination, source, glyph.width);
    }
    selected->cursor_x = selected_x + glyph.width + atlas_padding;
    selected->row_height =
        std::max(selected->row_height, glyph.height + atlas_padding);

    if (auto image = images.modify(selected->image)) {
        constexpr std::size_t channels = 4;
        auto pixels = std::make_unique<unsigned char[]>(
            selected->pixels.size() * channels
        );
        for (std::size_t index = 0; index < selected->pixels.size(); ++index) {
            const auto coverage = selected->pixels[index];
            pixels[index * channels] = coverage;
            pixels[index * channels + 1] = coverage;
            pixels[index * channels + 2] = coverage;
            pixels[index * channels + 3] = 255;
        }
        image->set_data(std::move(pixels));
    }
    const AtlasGlyph cached {
        .uv =
            {
                .min =
                    {
                        static_cast<float>(selected_x) / atlas_size,
                        static_cast<float>(selected_y) / atlas_size,
                    },
                .max =
                    {
                        static_cast<float>(selected_x + glyph.width) /
                            atlas_size,
                        static_cast<float>(selected_y + glyph.height) /
                            atlas_size,
                    },
            },
        .atlas = selected->image,
    };
    m_glyphs.emplace(key, cached);
    return cached;
}

void TextPipeline::layout(
    AssetId font_id,
    const Font& font,
    const TextMeasureInfo& measure,
    float font_size,
    const TextLayout& text_layout,
    Vector2 bounds,
    Assets<Image>& images,
    TextLayoutInfo& output
) {
    const Optional<float> max_width =
        text_layout.line_break == LineBreak::NoWrap ?
            nullopt :
            Optional<float> {bounds.x};
    const auto lines = measure.lines(max_width, text_layout.line_break);
    output.size = measure.compute_size(max_width, text_layout.line_break);
    output.glyphs.clear();
    if (font_size <= 0.0f || measure.glyphs.empty()) {
        return;
    }

    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const auto& line = lines[line_index];
        float x = text_layout.justify == Justify::Center ?
                      (bounds.x - line.width) * 0.5f :
                  text_layout.justify == Justify::Right ?
                      bounds.x - line.width :
                      0.0f;
        Vector2 pen {
            std::max(0.0f, x),
            measure.ascent +
                measure.line_height * static_cast<float>(line_index),
        };
        for (auto index = line.begin; index < line.end; ++index) {
            const auto& glyph = measure.glyphs[index];
            if (index > line.begin) {
                pen.x += glyph.kerning;
            }
            const auto rasterized = font.rasterize(glyph.glyph_id, font_size);
            if (rasterized.width > 0 && rasterized.height > 0) {
                const auto atlas = cache_glyph(
                    font_id,
                    font,
                    glyph.glyph_id,
                    font_size,
                    images
                );
                if (atlas.atlas) {
                    output.glyphs.push_back(
                        PositionedGlyph {
                            .position =
                                pen +
                                Vector2 {
                                    static_cast<float>(rasterized.offset_x),
                                    static_cast<float>(rasterized.offset_y),
                                },
                            .size =
                                {
                                    static_cast<float>(rasterized.width),
                                    static_cast<float>(rasterized.height),
                                },
                            .uv = atlas.uv,
                            .atlas = atlas.atlas,
                        }
                    );
                }
            }
            pen.x += glyph.advance;
        }
    }
}

} // namespace fei::text
