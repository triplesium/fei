#include "graphics_opengl/buffer.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/graphics_device.hpp"
#include "graphics_opengl/sampler.hpp"
#include "graphics_opengl/texture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

using namespace fei;

namespace {

TextureDescription make_texture_description() {
    return TextureDescription {
        .width = 16,
        .height = 8,
        .depth = 1,
        .mip_level = 3,
        .layer = 2,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = {TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D,
    };
}

class TrackingResource final : public DeferredResourceOpenGL {
  public:
    explicit TrackingResource(int id, std::vector<int>* destroyed = nullptr) :
        id(id), destroyed(destroyed) {}

    int id;
    std::vector<int>* destroyed;
    mutable int create_count {0};
    int destroy_count {0};

  private:
    void create_gl_resource() const override { ++create_count; }

    void destroy_gl_resource() override {
        ++destroy_count;
        if (destroyed != nullptr) {
            destroyed->push_back(id);
        }
    }
};

} // namespace

TEST_CASE(
    "OpenGL resources are CPU-only until explicitly materialized",
    "[graphics][opengl]"
) {
    BufferOpenGL buffer(
        BufferDescription {
            .size = 64,
            .usages = {BufferUsages::Vertex, BufferUsages::Dynamic},
        }
    );
    TextureOpenGL texture(make_texture_description());
    SamplerOpenGL sampler(SamplerDescription::Linear);

    REQUIRE_FALSE(buffer.created());
    REQUIRE(buffer.id() == 0);
    REQUIRE(buffer.size() == 64);
    REQUIRE(buffer.usages().is_set(BufferUsages::Vertex));
    REQUIRE(buffer.usages().is_set(BufferUsages::Dynamic));

    REQUIRE_FALSE(texture.created());
    REQUIRE(texture.id() == 0);
    REQUIRE(texture.width() == 16);
    REQUIRE(texture.height() == 8);
    REQUIRE(texture.mip_level() == 3);
    REQUIRE(texture.layer() == 2);
    REQUIRE(texture.format() == PixelFormat::Rgba8Unorm);

    REQUIRE_FALSE(sampler.created());
    REQUIRE(sampler.id() == 0);
}

TEST_CASE(
    "Deferred OpenGL resource lifecycle hooks run once",
    "[graphics][opengl]"
) {
    TrackingResource resource(1);

    REQUIRE_FALSE(resource.created());

    resource.ensure_created();
    resource.ensure_created();

    REQUIRE(resource.created());
    REQUIRE(resource.create_count == 1);

    resource.dispose();
    resource.dispose();

    REQUIRE_FALSE(resource.created());
    REQUIRE(resource.destroy_count == 1);
}

TEST_CASE(
    "OpenGL device state queues deferred work in FIFO order",
    "[graphics][opengl]"
) {
    OpenGLDeviceState state;

    state.enqueue_operation(
        OpenGLPendingBufferUpdate {
            .offset = 4,
            .data = {std::byte {0x01}, std::byte {0x02}},
        }
    );
    state.enqueue_operation(
        OpenGLPendingTextureUpdate {
            .data = {std::byte {0x03}},
            .x = 1,
            .width = 2,
            .height = 3,
            .depth = 1,
        }
    );
    state.enqueue_operation(OpenGLPendingCommandSubmit {});

    auto operations = state.take_pending_operations();
    REQUIRE(operations.size() == 3);
    REQUIRE(std::holds_alternative<OpenGLPendingBufferUpdate>(operations[0]));
    REQUIRE(std::holds_alternative<OpenGLPendingTextureUpdate>(operations[1]));
    REQUIRE(std::holds_alternative<OpenGLPendingCommandSubmit>(operations[2]));

    const auto& buffer_update =
        std::get<OpenGLPendingBufferUpdate>(operations[0]);
    REQUIRE(buffer_update.offset == 4);
    REQUIRE(buffer_update.data.size() == 2);
    REQUIRE(buffer_update.data[1] == std::byte {0x02});

    const auto& texture_update =
        std::get<OpenGLPendingTextureUpdate>(operations[1]);
    REQUIRE(texture_update.x == 1);
    REQUIRE(texture_update.width == 2);
    REQUIRE(texture_update.height == 3);
    REQUIRE(texture_update.data[0] == std::byte {0x03});

    REQUIRE(state.take_pending_operations().empty());
}

TEST_CASE(
    "OpenGL disposal queue ignores null resources and preserves order",
    "[graphics][opengl]"
) {
    OpenGLDeviceState state;
    std::vector<int> destroyed;

    state.enqueue_disposal(nullptr);
    REQUIRE(state.take_pending_disposals().empty());

    state.enqueue_disposal(std::make_unique<TrackingResource>(11, &destroyed));
    state.enqueue_disposal(std::make_unique<TrackingResource>(12, &destroyed));

    auto disposals = state.take_pending_disposals();
    REQUIRE(disposals.size() == 2);
    REQUIRE(state.take_pending_disposals().empty());

    for (auto& resource : disposals) {
        resource->ensure_created();
        resource->dispose();
    }

    REQUIRE(destroyed == std::vector<int> {11, 12});
}
