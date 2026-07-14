#include "graphics_opengl/deferred_resource.hpp"

#include "graphics_opengl/buffer.hpp"
#include "graphics_opengl/framebuffer.hpp"
#include "graphics_opengl/graphics_device.hpp"
#include "graphics_opengl/resource.hpp"
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
    "OpenGL queues ImGui texture uploads before overlay command submission",
    "[graphics][opengl][imgui]"
) {
    OpenGLDeviceState state;
    state.enqueue_operation(
        OpenGLPendingTextureUpdate {
            .data = {std::byte {0xff}},
            .width = 1,
            .height = 1,
            .depth = 1,
        }
    );
    state.enqueue_operation(OpenGLPendingCommandSubmit {});

    auto operations = state.take_pending_operations();
    REQUIRE(operations.size() == 2);
    REQUIRE(std::holds_alternative<OpenGLPendingTextureUpdate>(operations[0]));
    REQUIRE(std::holds_alternative<OpenGLPendingCommandSubmit>(operations[1]));
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

TEST_CASE(
    "OpenGL device state reuses cached framebuffer descriptions",
    "[graphics][opengl]"
) {
    OpenGLDeviceState state;
    auto color = std::make_shared<TextureOpenGL>(make_texture_description());
    auto depth_desc = make_texture_description();
    depth_desc.texture_format = PixelFormat::Depth32Float;
    depth_desc.texture_usage = {TextureUsage::DepthStencil};
    auto depth = std::make_shared<TextureOpenGL>(depth_desc);

    FramebufferDescription desc {
        .color_targets =
            {
                FramebufferAttachment {
                    .texture = color,
                    .mip_level = 0,
                    .layer = 0,
                },
            },
        .depth_target = FramebufferAttachment {
            .texture = depth,
            .mip_level = 0,
            .layer = 0,
        },
    };

    int create_count = 0;
    auto create = [&]() -> std::shared_ptr<Framebuffer> {
        ++create_count;
        return std::make_shared<Framebuffer>(desc);
    };

    auto first = state.get_or_create_framebuffer(desc, create);
    auto second = state.get_or_create_framebuffer(desc, create);

    REQUIRE(first == second);
    REQUIRE(create_count == 1);

    auto stats = state.resource_cache_stats();
    REQUIRE(stats.framebuffer_requests == 2);
    REQUIRE(stats.framebuffer_hits == 1);
    REQUIRE(stats.framebuffer_creates == 1);
    REQUIRE(stats.framebuffer_cache_size == 1);
}

TEST_CASE(
    "OpenGL framebuffer cache keys include attachment identity",
    "[graphics][opengl]"
) {
    OpenGLDeviceState state;
    auto first_color =
        std::make_shared<TextureOpenGL>(make_texture_description());
    auto second_color =
        std::make_shared<TextureOpenGL>(make_texture_description());

    auto first_desc = FramebufferDescription {
        .color_targets = {
            FramebufferAttachment {
                .texture = first_color,
                .mip_level = 0,
                .layer = 0,
            },
        },
    };
    auto second_desc = FramebufferDescription {
        .color_targets = {
            FramebufferAttachment {
                .texture = second_color,
                .mip_level = 0,
                .layer = 0,
            },
        },
    };

    int create_count = 0;
    auto create_first = [&]() -> std::shared_ptr<Framebuffer> {
        ++create_count;
        return std::make_shared<Framebuffer>(first_desc);
    };
    auto create_second = [&]() -> std::shared_ptr<Framebuffer> {
        ++create_count;
        return std::make_shared<Framebuffer>(second_desc);
    };

    auto first = state.get_or_create_framebuffer(first_desc, create_first);
    auto second = state.get_or_create_framebuffer(second_desc, create_second);

    REQUIRE(first != second);
    REQUIRE(create_count == 2);

    auto stats = state.resource_cache_stats();
    REQUIRE(stats.framebuffer_hits == 0);
    REQUIRE(stats.framebuffer_creates == 2);
    REQUIRE(stats.framebuffer_cache_size == 2);
}

TEST_CASE(
    "OpenGL device state reuses cached resource set descriptions",
    "[graphics][opengl]"
) {
    OpenGLDeviceState state;
    auto layout =
        std::make_shared<ResourceLayout>(ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {
                texture_read_only("albedo"),
                sampler("point_sampler"),
            }
        ));
    auto texture = std::make_shared<TextureOpenGL>(make_texture_description());
    auto sampler_resource =
        std::make_shared<SamplerOpenGL>(SamplerDescription::Point);

    ResourceSetDescription desc {
        .layout = layout,
        .resources = {texture, sampler_resource},
        .name = "test.resource_set",
    };

    int create_count = 0;
    auto create = [&]() -> std::shared_ptr<ResourceSet> {
        ++create_count;
        return std::make_shared<ResourceSet>(desc);
    };

    auto first = state.get_or_create_resource_set(desc, create);
    auto second = state.get_or_create_resource_set(desc, create);

    REQUIRE(first == second);
    REQUIRE(create_count == 1);

    auto stats = state.resource_cache_stats();
    REQUIRE(stats.resource_set_requests == 2);
    REQUIRE(stats.resource_set_hits == 1);
    REQUIRE(stats.resource_set_creates == 1);
    REQUIRE(stats.resource_set_cache_size == 1);
    REQUIRE(stats.resource_set_sources.size() == 1);
    REQUIRE(stats.resource_set_sources[0].name == "test.resource_set");
    REQUIRE(stats.resource_set_sources[0].requests == 2);
    REQUIRE(stats.resource_set_sources[0].hits == 1);
    REQUIRE(stats.resource_set_sources[0].creates == 1);
    REQUIRE(stats.resource_set_sources[0].cache_size == 1);

    state.clear_resource_cache();
    REQUIRE(state.resource_cache_stats().resource_set_cache_size == 0);

    auto third = state.get_or_create_resource_set(desc, create);
    REQUIRE(third != first);
    REQUIRE(create_count == 2);
}

TEST_CASE(
    "Queued OpenGL commands retain frame resources until CPU execution",
    "[graphics][opengl][command-buffer][lifetime]"
) {
    OpenGLDeviceState state;
    auto uniform = std::make_shared<BufferOpenGL>(BufferDescription {
        .size = 64,
        .usages = BufferUsages::Uniform,
    });
    auto sampled_texture =
        std::make_shared<TextureOpenGL>(make_texture_description());
    auto target_description = make_texture_description();
    target_description.texture_usage = TextureUsage::RenderTarget;
    auto target = std::make_shared<TextureOpenGL>(target_description);
    auto layout = std::make_shared<ResourceLayoutOpenGL>(
        ResourceLayoutDescription::sequencial(
            ShaderStages::Fragment,
            {
                uniform_buffer("frame_uniform"),
                texture_read_only("frame_texture"),
            }
        )
    );
    auto resource_set =
        std::make_shared<ResourceSetOpenGL>(ResourceSetDescription {
            .layout = layout,
            .resources = {uniform, sampled_texture},
            .name = "frame resources",
        });
    auto framebuffer = std::make_shared<Framebuffer>(FramebufferDescription {
        .color_targets = {FramebufferAttachment {.texture = target}},
    });

    std::weak_ptr<const Buffer> weak_uniform = uniform;
    std::weak_ptr<const Texture> weak_sampled_texture = sampled_texture;
    std::weak_ptr<const Texture> weak_target = target;
    std::weak_ptr<const ResourceLayout> weak_layout = layout;

    std::vector<opengl_commands::Command> commands;
    commands.emplace_back(
        opengl_commands::BeginRenderPass {
            .desc = RenderPassDescription {.framebuffer = framebuffer},
        }
    );
    commands.emplace_back(
        opengl_commands::SetResourceSet {
            .slot = 0,
            .resource_set = resource_set,
            .dynamic_offsets = {},
        }
    );
    commands.emplace_back(
        opengl_commands::UpdateBuffer {
            .buffer = uniform,
            .offset = 0,
            .data = std::vector<std::byte>(16),
        }
    );
    state.enqueue_operation(
        OpenGLPendingCommandSubmit {.commands = std::move(commands)}
    );

    uniform.reset();
    sampled_texture.reset();
    target.reset();
    layout.reset();
    resource_set.reset();
    framebuffer.reset();

    CHECK_FALSE(weak_uniform.expired());
    CHECK_FALSE(weak_sampled_texture.expired());
    CHECK_FALSE(weak_target.expired());
    CHECK_FALSE(weak_layout.expired());

    auto pending = state.take_pending_operations();
    REQUIRE(pending.size() == 1);
    CHECK_FALSE(weak_uniform.expired());
    CHECK_FALSE(weak_sampled_texture.expired());
    CHECK_FALSE(weak_target.expired());
    CHECK_FALSE(weak_layout.expired());

    pending.clear();
    CHECK(weak_uniform.expired());
    CHECK(weak_sampled_texture.expired());
    CHECK(weak_target.expired());
    CHECK(weak_layout.expired());
}
