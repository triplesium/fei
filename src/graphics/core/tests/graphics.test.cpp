#include "ecs/schedule.hpp"
#include "ecs/system_params.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/mapped_resource.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "graphics/swapchain.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "graphics/texture_view.hpp"
#include "graphics/utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace fei;

namespace {

void read_graphics_device_system(ResRO<GraphicsDevice>) {}

void read_graphics_device_again_system(ResRO<GraphicsDevice>) {}

void write_graphics_device_system(ResRW<GraphicsDevice>) {}

static_assert(std::is_same_v<
              decltype(std::declval<const TextureView&>().target()),
              std::shared_ptr<const Texture>>);
static_assert(std::is_same_v<
              decltype(std::declval<MappedResource&>().resource()),
              std::shared_ptr<MappableResource>>);
static_assert(std::is_same_v<
              decltype(std::declval<const MappedResource&>().resource()),
              std::shared_ptr<const MappableResource>>);
static_assert(std::is_same_v<
              decltype(std::declval<const Texture&>()
                           .full_view(std::declval<const GraphicsDevice&>())),
              std::shared_ptr<const TextureView>>);
static_assert(std::is_same_v<
              decltype(std::declval<const Swapchain&>().framebuffer()),
              std::shared_ptr<const Framebuffer>>);
static_assert(std::is_same_v<
              decltype(std::declval<const GraphicsDevice&>()
                           .present(std::declval<const Swapchain&>())),
              void>);
static_assert(std::is_same_v<
              decltype(std::declval<MainSwapchain&>().swapchain),
              std::shared_ptr<Swapchain>>);

TextureDescription make_texture_description(
    std::uint32_t width = 64,
    std::uint32_t height = 32,
    std::uint32_t depth = 8,
    std::uint32_t mip_level = 4,
    std::uint32_t layer = 2,
    PixelFormat format = PixelFormat::Rgba8Unorm
) {
    return TextureDescription {
        .width = width,
        .height = height,
        .depth = depth,
        .mip_level = mip_level,
        .layer = layer,
        .texture_format = format,
        .texture_usage = {TextureUsage::Sampled},
        .texture_type = TextureType::Texture2D
    };
}

class FakeTexture : public Texture {
  private:
    TextureDescription m_desc;

  public:
    explicit FakeTexture(TextureDescription desc) : m_desc(desc) {}

    PixelFormat format() const override { return m_desc.texture_format; }
    uint32 width() const override { return m_desc.width; }
    uint32 height() const override { return m_desc.height; }
    uint32 depth() const override { return m_desc.depth; }
    uint32 mip_level() const override { return m_desc.mip_level; }
    uint32 layer() const override { return m_desc.layer; }
    BitFlags<TextureUsage> usage() const override {
        return m_desc.texture_usage;
    }
    TextureType type() const override { return m_desc.texture_type; }
    TextureSampleCount sample_count() const override {
        return m_desc.sample_count;
    }
};

class FakeBuffer : public Buffer {
  private:
    BufferDescription m_desc;

  public:
    explicit FakeBuffer(BufferDescription desc) : m_desc(desc) {}

    std::size_t size() const override { return m_desc.size; }
    BitFlags<BufferUsages> usages() const override { return m_desc.usages; }
};

class FakeGraphicsDevice : public GraphicsDevice {
  public:
    mutable std::vector<TextureViewDescription> texture_view_requests;

    std::shared_ptr<ShaderModule>
    create_shader_module(const ShaderDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<Buffer>
    create_buffer(const BufferDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<Texture>
    create_texture(const TextureDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<TextureView>
    create_texture_view(const TextureViewDescription& desc) const override {
        texture_view_requests.push_back(desc);
        return std::make_shared<TextureView>(desc);
    }

    std::shared_ptr<CommandBuffer> create_command_buffer() const override {
        return nullptr;
    }

    std::shared_ptr<Pipeline>
    create_render_pipeline(const RenderPipelineDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<Pipeline>
    create_compute_pipeline(const ComputePipelineDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<Framebuffer>
    create_framebuffer(const FramebufferDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<ResourceLayout>
    create_resource_layout(const ResourceLayoutDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<ResourceSet>
    create_resource_set(const ResourceSetDescription&) const override {
        return nullptr;
    }

    std::shared_ptr<Sampler>
    create_sampler(const SamplerDescription&) const override {
        return nullptr;
    }

    void submit_commands(std::shared_ptr<CommandBuffer>) const override {}

    void update_texture(
        std::shared_ptr<Texture>,
        const void*,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t
    ) const override {}

    void update_buffer(
        std::shared_ptr<Buffer>,
        std::uint32_t,
        const void*,
        std::uint32_t
    ) const override {}

    MappedResource
    map(std::shared_ptr<MappableResource>, MapMode) const override {
        return MappedResource(nullptr, MapMode::Read, {});
    }

    void unmap(std::shared_ptr<MappableResource>) const override {}

    std::shared_ptr<TextureReadback>
    create_texture_readback(uint32 = 3) const override {
        return nullptr;
    }

    void present(const Swapchain&) const override {}
};

} // namespace

TEST_CASE(
    "GraphicsDevice is a worker-readable ECS resource",
    "[graphics][ecs]"
) {
    FunctionSystem<decltype(read_graphics_device_system)*> read_device(
        read_graphics_device_system
    );

    REQUIRE_FALSE(read_device.access().main_thread_only);
    REQUIRE_FALSE(read_device.access().is_barrier());
    REQUIRE(
        read_device.access().read_resources.contains(type_id<GraphicsDevice>())
    );

    SECTION("read-only device systems share a batch") {
        Schedule schedule;
        schedule.add_systems(
            read_graphics_device_system,
            read_graphics_device_again_system
        );
        schedule.sort_systems();

        REQUIRE(schedule.execution_batches().size() == 1);
        REQUIRE(schedule.execution_batches().front().size() == 2);
    }

    SECTION("mutable device systems still conflict with readers") {
        Schedule schedule;
        schedule.add_systems(
            read_graphics_device_system,
            write_graphics_device_system
        );
        schedule.sort_systems();

        REQUIRE(schedule.execution_batches().size() == 2);
        REQUIRE(schedule.execution_batches()[0].size() == 1);
        REQUIRE(schedule.execution_batches()[1].size() == 1);
    }
}

TEST_CASE(
    "Graphics utils compute texture mip dimensions",
    "[graphics][utils]"
) {
    SECTION("single dimensions clamp to one") {
        REQUIRE(Utils::get_dimension(128, 0) == 128);
        REQUIRE(Utils::get_dimension(128, 3) == 16);
        REQUIRE(Utils::get_dimension(3, 8) == 1);
        REQUIRE(Utils::get_dimension(0, 0) == 1);
    }

    SECTION("texture dimensions use each axis independently") {
        auto texture =
            std::make_shared<FakeTexture>(make_texture_description(17, 9, 3));

        auto [width, height, depth] = Utils::get_mip_dimensions(texture, 3);

        REQUIRE(width == 2);
        REQUIRE(height == 1);
        REQUIRE(depth == 1);
    }
}

TEST_CASE(
    "Graphics resource layout helpers preserve resource metadata",
    "[graphics][resource]"
) {
    SECTION("helpers create the expected resource kinds") {
        auto uniform = uniform_buffer("camera");
        auto sampled = texture_read_only("albedo");
        auto storage_texture = texture_read_write("output");
        auto read_buffer = storage_buffer_read_only("particles");
        auto write_buffer = storage_buffer_read_write("visible_particles");
        auto linear_sampler = sampler("linear_sampler");

        REQUIRE(uniform.name == "camera");
        REQUIRE(uniform.kind == ResourceKind::UniformBuffer);
        REQUIRE(uniform.array_count == 1);
        REQUIRE_FALSE(uniform.options);
        REQUIRE(sampled.name == "albedo");
        REQUIRE(sampled.kind == ResourceKind::TextureReadOnly);
        REQUIRE(storage_texture.name == "output");
        REQUIRE(storage_texture.kind == ResourceKind::TextureReadWrite);
        REQUIRE(read_buffer.name == "particles");
        REQUIRE(read_buffer.kind == ResourceKind::StorageBufferReadOnly);
        REQUIRE(write_buffer.name == "visible_particles");
        REQUIRE(write_buffer.kind == ResourceKind::StorageBufferReadWrite);
        REQUIRE(linear_sampler.name == "linear_sampler");
        REQUIRE(linear_sampler.kind == ResourceKind::Sampler);
    }

    SECTION("sequencial assigns contiguous bindings and shared stages") {
        BitFlags<ShaderStages> stages {
            ShaderStages::Vertex,
            ShaderStages::Fragment,
        };

        auto desc = ResourceLayoutDescription::sequencial(
            stages,
            {
                uniform_buffer("camera"),
                texture_read_only("albedo"),
                sampler("linear_sampler"),
            }
        );

        REQUIRE(desc.elements.size() == 3);
        REQUIRE(desc.elements[0].binding == 0);
        REQUIRE(desc.elements[1].binding == 1);
        REQUIRE(desc.elements[2].binding == 2);
        REQUIRE(desc.elements[0].stages == stages);
        REQUIRE(desc.elements[1].stages == stages);
        REQUIRE(desc.elements[2].stages == stages);
        REQUIRE(desc.elements[0].name == "camera");
        REQUIRE(desc.elements[1].name == "albedo");
        REQUIRE(desc.elements[2].name == "linear_sampler");
        REQUIRE_NOTHROW(ResourceLayout(desc));
    }

    SECTION("layout elements expose Vulkan descriptor metadata") {
        auto dynamic_uniform = uniform_buffer("camera");
        dynamic_uniform.array_count = 2;
        dynamic_uniform.options.set(
            ResourceLayoutElementOptions::DynamicBinding
        );

        auto desc = ResourceLayoutDescription {
            .elements = {std::move(dynamic_uniform)},
        };

        REQUIRE(desc.elements[0].array_count == 2);
        REQUIRE(desc.elements[0].options.is_set(
            ResourceLayoutElementOptions::DynamicBinding
        ));
        REQUIRE_NOTHROW(ResourceLayout(desc));
    }

    SECTION("buffer ranges bind a sub-range of a buffer") {
        auto buffer = std::make_shared<FakeBuffer>(BufferDescription {
            .size = 256,
            .usages = BufferUsages::Uniform,
        });

        auto range = BufferRange(buffer, 64, 128);

        REQUIRE(range.buffer() == buffer);
        REQUIRE(range.offset() == 64);
        REQUIRE(range.size() == 128);
    }

    SECTION("explicit bindings may be sparse and out of declaration order") {
        auto desc = ResourceLayoutDescription {
            .elements = {
                ResourceLayoutElementDescription {
                    .binding = 4,
                    .name = "source",
                    .kind = ResourceKind::TextureReadOnly,
                    .stages = ShaderStages::Fragment,
                },
                ResourceLayoutElementDescription {
                    .binding = 0,
                    .name = "Constants",
                    .kind = ResourceKind::UniformBuffer,
                    .stages = ShaderStages::Fragment,
                },
            },
        };

        REQUIRE_NOTHROW(ResourceLayout(desc));
    }
}

TEST_CASE(
    "Graphics lightweight objects expose their descriptions",
    "[graphics]"
) {
    auto texture = std::make_shared<FakeTexture>(
        make_texture_description(64, 32, 8, 4, 2, PixelFormat::Rgba8Unorm)
    );

    SECTION("ShaderModule stores its stage") {
        ShaderModule shader({
            .stage = ShaderStages::Compute,
            .source = "void main() {}",
        });

        REQUIRE(shader.stage() == ShaderStages::Compute);
    }

    SECTION("TextureView defaults to target format") {
        TextureView view({
            .target = texture,
            .base_mip_level = 1,
            .mip_levels = 2,
            .base_array_layer = 3,
            .array_layers = 4,
        });

        REQUIRE(view.target() == texture);
        REQUIRE(view.base_mip_level() == 1);
        REQUIRE(view.mip_levels() == 2);
        REQUIRE(view.base_array_layer() == 3);
        REQUIRE(view.array_layers() == 4);
        REQUIRE(view.format() == PixelFormat::Rgba8Unorm);
    }

    SECTION("TextureView accepts an override format") {
        TextureView view({
            .target = texture,
            .format = PixelFormat::R8Unorm,
        });

        REQUIRE(view.format() == PixelFormat::R8Unorm);
    }

    SECTION("Framebuffer stores color and depth attachments") {
        auto depth_texture = std::make_shared<FakeTexture>(
            make_texture_description(64, 32, 1, 1, 1, PixelFormat::Depth32Float)
        );
        Framebuffer framebuffer({
            .color_targets =
                {
                    FramebufferAttachment {
                        .texture = texture,
                        .mip_level = 2,
                        .layer = 1,
                    },
                },
            .depth_target = FramebufferAttachment {
                .texture = depth_texture,
                .mip_level = 0,
                .layer = 3,
            },
        });

        REQUIRE(framebuffer.color_attachments().size() == 1);
        REQUIRE(framebuffer.color_attachments()[0].texture == texture);
        REQUIRE(framebuffer.color_attachments()[0].mip_level == 2);
        REQUIRE(framebuffer.color_attachments()[0].layer == 1);
        REQUIRE(framebuffer.depth_attachment().has_value());
        REQUIRE(framebuffer.depth_attachment()->texture == depth_texture);
        REQUIRE(framebuffer.depth_attachment()->layer == 3);
        REQUIRE(framebuffer.output_description().color_attachments.size() == 1);
        REQUIRE(
            framebuffer.output_description().color_attachments[0].format ==
            PixelFormat::Rgba8Unorm
        );
        REQUIRE(
            framebuffer.output_description().sample_count ==
            TextureSampleCount::Count1
        );
        REQUIRE(framebuffer.output_description()
                    .depth_stencil_attachment.has_value());
        REQUIRE(
            framebuffer.output_description().depth_stencil_attachment->format ==
            PixelFormat::Depth32Float
        );
    }

    SECTION("MappedResource exposes its resource, mode, and bytes") {
        std::vector<std::byte> bytes {
            std::byte {0x01},
            std::byte {0x02},
            std::byte {0x03},
        };

        MappedResource mapped(
            texture,
            MapMode::ReadWrite,
            std::span<std::byte>(bytes.data(), bytes.size())
        );

        REQUIRE(mapped.resource() == texture);
        REQUIRE(mapped.map_mode() == MapMode::ReadWrite);
        REQUIRE(mapped.data().size() == bytes.size());
        REQUIRE(mapped.data()[0] == std::byte {0x01});
        REQUIRE(mapped.data()[2] == std::byte {0x03});
    }
}

TEST_CASE("Graphics texture views are resolved and cached", "[graphics]") {
    FakeGraphicsDevice device;
    auto texture = std::make_shared<FakeTexture>(
        make_texture_description(128, 64, 1, 5, 3, PixelFormat::Rg8Unorm)
    );

    SECTION("Texture::full_view creates one full-range view") {
        auto first = texture->full_view(device);
        auto second = texture->full_view(device);

        REQUIRE(first == second);
        REQUIRE(device.texture_view_requests.size() == 1);

        const auto& desc = device.texture_view_requests[0];
        REQUIRE(desc.target == texture);
        REQUIRE(desc.base_mip_level == 0);
        REQUIRE(desc.mip_levels == 5);
        REQUIRE(desc.base_array_layer == 0);
        REQUIRE(desc.array_layers == 3);
        REQUIRE(desc.format.has_value());
        REQUIRE(*desc.format == PixelFormat::Rg8Unorm);
    }

    SECTION("GraphicsDevice::get_texture_view returns existing texture views") {
        auto texture_view =
            std::make_shared<TextureView>(TextureViewDescription {
                .target = texture,
                .format = PixelFormat::R8Unorm,
            });

        auto resolved = device.get_texture_view(texture_view);

        REQUIRE(resolved == texture_view);
        REQUIRE(device.texture_view_requests.empty());
    }

    SECTION(
        "GraphicsDevice::get_texture_view creates full views for textures"
    ) {
        auto resolved = device.get_texture_view(texture);

        REQUIRE(resolved != nullptr);
        REQUIRE(resolved->target() == texture);
        REQUIRE(device.texture_view_requests.size() == 1);
    }
}
