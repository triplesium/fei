#pragma once

#include "base/optional.hpp"
#include "base/types.hpp"
#include "math/color.hpp"
#include "math/vector.hpp"
#include "rendering/gpu_vector.hpp"
#include "text/text.hpp"
#include "ui/image.hpp"
#include "ui/node.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ets {

class Buffer;
class ResourceLayout;
class ResourceSet;
class Sampler;
enum class CachedRenderPipelineId : uint32;
enum class PixelFormat : uint8;

namespace ui::rendering {

struct Vertex {
    Vector2 position;
    Vector2 uv;
    Color4F color;
    Vector2 local_position;
    Vector2 size;
    Vector4 border_radius;
    Vector4 border;
    float kind {0.0f};
    float border_side {0.0f};
};

enum class BorderSide : uint8 {
    Left = 1,
    Top = 2,
    Right = 3,
    Bottom = 4,
};

struct Quad {
    std::array<Vertex, 4> vertices;
    std::array<std::uint32_t, 6> indices;
};

struct Batch {
    std::shared_ptr<const ResourceSet> texture_set;
    uint32 first_index {0};
    uint32 index_count {0};
};

[[nodiscard]] Optional<Quad> make_quad(
    const ComputedNode& node,
    const BackgroundColor& background,
    Optional<Rect> clip = nullopt
);

[[nodiscard]] Optional<Quad> make_image_quad(
    const ComputedNode& node,
    const ImageNode& image,
    Vector2 texture_size,
    Optional<Rect> clip = nullopt
);

[[nodiscard]] Optional<Quad> make_border_quad(
    const ComputedNode& node,
    Color4F color,
    BorderSide side,
    Optional<Rect> clip = nullopt
);

[[nodiscard]] Optional<Quad> make_glyph_quad(
    const ComputedNode& node,
    const text::PositionedGlyph& glyph,
    Color4F color,
    Optional<Rect> clip = nullopt
);

struct Phase {
    GpuVector<Vertex> vertices {BufferUsages::Vertex};
    GpuVector<std::uint32_t> indices {BufferUsages::Index};
    std::vector<Batch> batches;
    uint32 glyph_count {0};
    uint32 glyph_batch_count {0};
    bool active {false};

    void clear();
    void append(
        const Quad& quad,
        std::shared_ptr<const ResourceSet> texture_set = nullptr
    );
    void append_glyph(
        const Quad& quad,
        std::shared_ptr<const ResourceSet> texture_set = nullptr
    );
};

struct RenderState {
    std::shared_ptr<ResourceLayout> frame_layout;
    std::shared_ptr<ResourceLayout> texture_layout;
    std::shared_ptr<Buffer> frame_uniform_buffer;
    std::shared_ptr<ResourceSet> frame_resource_set;
    std::shared_ptr<Sampler> default_sampler;
    std::optional<PixelFormat> pipeline_format;
    std::optional<CachedRenderPipelineId> pipeline_id;
};

} // namespace ui::rendering
} // namespace ets
