#include "graphics_vulkan/command_buffer_resource_retention.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace fei;
using namespace fei::vulkan_detail;

namespace {

class TestBuffer : public Buffer {
  public:
    std::size_t size() const override { return 64; }
    BitFlags<BufferUsages> usages() const override {
        return BufferUsages::Uniform;
    }
};

class TestTexture : public Texture {
  public:
    PixelFormat format() const override { return PixelFormat::Rgba8Unorm; }
    uint32 width() const override { return 16; }
    uint32 height() const override { return 16; }
    uint32 depth() const override { return 1; }
    uint32 mip_level() const override { return 1; }
    uint32 layer() const override { return 1; }
    BitFlags<TextureUsage> usage() const override {
        return {TextureUsage::RenderTarget, TextureUsage::Sampled};
    }
    TextureType type() const override { return TextureType::Texture2D; }
    TextureSampleCount sample_count() const override {
        return TextureSampleCount::Count1;
    }
};

} // namespace

TEST_CASE(
    "Vulkan command buffer retention releases descriptor and target resources "
    "only after completion",
    "[graphics][vulkan][command-buffer][lifetime]"
) {
    auto uniform = std::make_shared<TestBuffer>();
    auto sampled_texture = std::make_shared<TestTexture>();
    auto target = std::make_shared<TestTexture>();
    auto staging = std::make_shared<TestBuffer>();
    std::weak_ptr<const Buffer> weak_uniform = uniform;
    std::weak_ptr<const Texture> weak_sampled_texture = sampled_texture;
    std::weak_ptr<const Texture> weak_target = target;
    std::weak_ptr<const Buffer> weak_staging = staging;

    auto layout =
        std::make_shared<ResourceLayout>(ResourceLayoutDescription {});
    std::weak_ptr<const ResourceLayout> weak_layout = layout;
    auto resource_set = std::make_shared<ResourceSet>(ResourceSetDescription {
        .layout = layout,
        .resources = {uniform, sampled_texture},
        .name = "frame resources",
    });
    auto framebuffer = std::make_shared<Framebuffer>(FramebufferDescription {
        .color_targets = {FramebufferAttachment {.texture = target}},
    });

    CommandBufferResourceRetention retention;
    retention.retain_resource_set(resource_set);
    retention.retain_framebuffer(framebuffer);
    retention.retain_transient_buffer(staging);

    uniform.reset();
    sampled_texture.reset();
    target.reset();
    staging.reset();
    resource_set.reset();
    framebuffer.reset();
    layout.reset();

    CHECK_FALSE(weak_uniform.expired());
    CHECK_FALSE(weak_sampled_texture.expired());
    CHECK_FALSE(weak_target.expired());
    CHECK_FALSE(weak_staging.expired());
    CHECK_FALSE(weak_layout.expired());

    retention.clear();

    CHECK(weak_uniform.expired());
    CHECK(weak_sampled_texture.expired());
    CHECK(weak_target.expired());
    CHECK(weak_staging.expired());
    CHECK(weak_layout.expired());
}

TEST_CASE(
    "Each Vulkan in-flight frame releases only its own retained resources",
    "[graphics][vulkan][command-buffer][lifetime][frames-in-flight]"
) {
    std::array<CommandBufferResourceRetention, 3> frames;
    std::array<std::weak_ptr<const Buffer>, 3> weak_uniforms;

    for (std::size_t index = 0; index < frames.size(); ++index) {
        auto uniform = std::make_shared<TestBuffer>();
        weak_uniforms[index] = uniform;
        frames[index].retain_transient_buffer(std::move(uniform));
    }

    frames[1].clear();
    CHECK_FALSE(weak_uniforms[0].expired());
    CHECK(weak_uniforms[1].expired());
    CHECK_FALSE(weak_uniforms[2].expired());

    frames[0].clear();
    frames[2].clear();
    CHECK(weak_uniforms[0].expired());
    CHECK(weak_uniforms[2].expired());
}
