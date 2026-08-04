#include "imgui/renderer.hpp"

#include "asset/assets.hpp"
#include "core/image.hpp"
#include "graphics/enums.hpp"
#include "graphics/swapchain.hpp"
#include "imgui/texture.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstring>
#include <imgui.h>
#include <memory>
#include <thread>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class TripleFrameDevice : public FakeGraphicsDevice {
  public:
    std::size_t max_frames_in_flight() const override { return 3; }
};

std::shared_ptr<Texture> make_texture(FakeGraphicsDevice& device) {
    return device.create_texture(
        TextureDescription {
            .width = 4,
            .height = 4,
            .depth = 1,
            .mip_level = 1,
            .layer = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage = TextureUsage::Sampled,
            .texture_type = TextureType::Texture2D,
        }
    );
}

Handle<Image> make_image_handle(Assets<Image>& images) {
    return images.add(
        Image::create_empty(
            1,
            1,
            1,
            PixelFormat::Rgba8Unorm,
            TextureUsage::Sampled,
            TextureType::Texture2D
        )
    );
}

void begin_test_frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(320.0f, 200.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    ImGui::NewFrame();
    ImGui::Begin("Renderer test");
    ImGui::TextUnformatted("fei-imgui");
    ImGui::End();
}

} // namespace

TEST_CASE("ImGui image handles keep stable extracted IDs", "[imgui][texture]") {
    Assets<Image> assets(nullptr);
    ImGuiImages images;
    const auto image = make_image_handle(assets);

    const auto first = images.register_image(image);
    const auto second = images.register_image(image);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first != second);
    REQUIRE(images.contains(first));
    REQUIRE(images.size() == 2);

    const auto extracted = images.extract();
    REQUIRE(extracted.bindings.size() == 2);
    for (const auto& binding : extracted.bindings) {
        REQUIRE(binding.image_id == image.id());
        REQUIRE(
            (binding.texture_id == first.raw_id() ||
             binding.texture_id == second.raw_id())
        );
    }

    REQUIRE(images.unregister_image(first));
    REQUIRE_FALSE(images.unregister_image(first));
    REQUIRE_FALSE(images.contains(first));
    REQUIRE(images.contains(second));

    ImGui::CreateContext();
    begin_test_frame();
    ImGui::Begin("Image handle test");
    ImGui::Image(second.texture_id(), ImVec2(32.0f, 32.0f));
    ImGui::End();
    const auto frame = capture_imgui_frame_snapshot();
    REQUIRE(frame);
    ImGui::DestroyContext();
}

TEST_CASE("ImGui render texture handles stay stable", "[imgui][texture]") {
    ImGuiRenderTextures textures;

    const auto first = textures.reserve_texture();
    const auto second = textures.reserve_texture();
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first != second);
    REQUIRE(textures.contains(first));
    REQUIRE(textures.contains(second));
    REQUIRE(textures.size() == 2);

    REQUIRE(textures.release_texture(first));
    REQUIRE_FALSE(textures.release_texture(first));
    REQUIRE_FALSE(textures.contains(first));
    REQUIRE(textures.contains(second));
}

TEST_CASE(
    "ImGui texture registry binds reserved render texture handles",
    "[imgui][texture]"
) {
    FakeGraphicsDevice device;
    ImGuiTextureRegistry registry;
    ImGuiRenderer renderer;
    ImGuiRenderTextures textures;
    renderer.initialize(device, registry);

    const auto handle = textures.reserve_texture();
    const auto texture = make_texture(device);
    const auto sampler = device.create_sampler(SamplerDescription::Linear);

    registry.bind_render_texture(handle, texture, sampler);
    REQUIRE(registry.contains(handle.texture_id()));

    registry.unbind_render_texture(handle);
    REQUIRE(registry.pending_removal(handle.texture_id()));
    renderer.shutdown(registry);
}

TEST_CASE(
    "ImGui image bindings use fallback and survive GPU image replacement",
    "[imgui][texture]"
) {
    FakeGraphicsDevice device;
    PipelineCache pipeline_cache(device);
    RenderFrameContext frame_context;
    MainSwapchain main_swapchain {};
    ImGuiTextureRegistry registry;
    ImGuiRenderer renderer;
    renderer.initialize(device, registry);

    Assets<Image> assets(nullptr);
    ImGuiImages images;
    const auto image = make_image_handle(assets);
    const auto texture_handle = images.register_image(image);
    RenderAssets<GpuImage> gpu_images;
    RenderingDefaults defaults {.default_texture = make_texture(device)};

    auto extracted = images.extract();
    registry.sync_images(device, extracted, gpu_images, defaults);
    REQUIRE(registry.contains(texture_handle.texture_id()));
    const auto fallback_resource_sets = device.resource_set_descriptions.size();

    registry.sync_images(device, extracted, gpu_images, defaults);
    REQUIRE(device.resource_set_descriptions.size() == fallback_resource_sets);

    auto first_gpu_texture = make_texture(device);
    auto first_gpu_sampler = device.create_sampler(SamplerDescription::Linear);
    gpu_images.insert(
        image.id(),
        std::make_unique<GpuImage>(first_gpu_texture, first_gpu_sampler)
    );
    registry.sync_images(device, extracted, gpu_images, defaults);
    REQUIRE(
        device.resource_set_descriptions.size() == fallback_resource_sets + 1
    );

    gpu_images.remove(image.id());
    gpu_images.insert(
        image.id(),
        std::make_unique<GpuImage>(
            make_texture(device),
            device.create_sampler(SamplerDescription::Linear)
        )
    );
    registry.sync_images(device, extracted, gpu_images, defaults);
    REQUIRE(
        device.resource_set_descriptions.size() == fallback_resource_sets + 2
    );
    REQUIRE(registry.contains(texture_handle.texture_id()));

    REQUIRE(images.unregister_image(texture_handle));
    extracted = images.extract();
    registry.sync_images(device, extracted, gpu_images, defaults);
    REQUIRE(registry.pending_removal(texture_handle.texture_id()));

    renderer.render(
        device,
        pipeline_cache,
        frame_context,
        main_swapchain,
        registry,
        ImGuiFrameSnapshot {}
    );
    REQUIRE_FALSE(registry.contains(texture_handle.texture_id()));
    renderer.shutdown(registry);
}

TEST_CASE("ImGui clip rectangles honor display origin and HiDPI", "[imgui]") {
    const auto scissor = calculate_imgui_scissor(
        Vector4(11.0f, 22.0f, 21.0f, 32.0f),
        Vector2(10.0f, 20.0f),
        Vector2(2.0f, 2.0f),
        100,
        80
    );

    REQUIRE(scissor);
    REQUIRE(
        (*scissor == ImGuiScissor {.x = 2, .y = 4, .width = 20, .height = 20})
    );
}

TEST_CASE("ImGui clip rectangles clamp and reject empty regions", "[imgui]") {
    const auto clamped = calculate_imgui_scissor(
        Vector4(-20.0f, -10.0f, 150.0f, 90.0f),
        Vector2(0.0f, 0.0f),
        Vector2(1.0f, 1.0f),
        100,
        50
    );
    REQUIRE(clamped);
    REQUIRE(
        (*clamped == ImGuiScissor {.x = 0, .y = 0, .width = 100, .height = 50})
    );
    REQUIRE_FALSE(calculate_imgui_scissor(
        Vector4(120.0f, 60.0f, 140.0f, 80.0f),
        Vector2(0.0f, 0.0f),
        Vector2(1.0f, 1.0f),
        100,
        50
    ));
}

TEST_CASE("ImGui draw offsets include prior command lists", "[imgui]") {
    ImDrawCmd draw_command;
    draw_command.IdxOffset = 3;
    draw_command.VtxOffset = 4;

    REQUIRE(
        (calculate_imgui_draw_offsets(6, 10, draw_command) ==
         ImGuiDrawOffsets {.first_index = 9, .vertex_offset = 14})
    );
}

TEST_CASE("ImGui buffer ring uses power-of-two growth and rotates", "[imgui]") {
    constexpr std::size_t initial_capacity = std::size_t {64} * 1024;
    constexpr std::size_t grown_capacity = std::size_t {128} * 1024;
    REQUIRE(imgui_buffer_capacity(1) == initial_capacity);
    REQUIRE(imgui_buffer_capacity(initial_capacity + 1) == grown_capacity);
    REQUIRE(imgui_frame_slot(0, 3) == 0);
    REQUIRE(imgui_frame_slot(1, 3) == 1);
    REQUIRE(imgui_frame_slot(2, 3) == 2);
    REQUIRE(imgui_frame_slot(3, 3) == 0);

    TripleFrameDevice device;
    ImGuiTextureRegistry registry;
    ImGuiRenderer renderer;
    renderer.initialize(device, registry);

    REQUIRE(renderer.frame_slot_count() == 3);
    for (std::size_t slot = 0; slot < 3; ++slot) {
        REQUIRE(renderer.vertex_capacity(slot) == initial_capacity);
        REQUIRE(renderer.index_capacity(slot) == initial_capacity);
    }

    std::weak_ptr<Buffer> first_buffer = device.buffers.front();
    renderer.shutdown(registry);
    device.buffers.clear();
    REQUIRE(first_buffer.expired());
}

TEST_CASE(
    "ImGui texture registry delays removal and never reuses IDs",
    "[imgui]"
) {
    FakeGraphicsDevice device;
    PipelineCache pipeline_cache(device);
    RenderFrameContext frame_context;
    MainSwapchain main_swapchain {};
    ImGuiTextureRegistry registry;
    ImGuiRenderer renderer;
    renderer.initialize(device, registry);

    const auto first = registry.register_texture(make_texture(device));
    registry.unregister_texture(first);
    REQUIRE(registry.contains(first));
    REQUIRE(registry.pending_removal(first));

    ImGui::CreateContext();
    begin_test_frame();
    const auto snapshot = capture_imgui_frame_snapshot();
    ImGui::DestroyContext();
    std::thread render_worker([&]() {
        renderer.render(
            device,
            pipeline_cache,
            frame_context,
            main_swapchain,
            registry,
            *snapshot
        );
    });
    render_worker.join();
    REQUIRE_FALSE(registry.contains(first));

    const auto second = registry.register_texture(make_texture(device));
    REQUIRE(second > first);

    renderer.shutdown(registry);
}

TEST_CASE(
    "ImGui managed textures create, repack partial updates, and destroy",
    "[imgui][texture]"
) {
    FakeGraphicsDevice device;
    PipelineCache pipeline_cache(device);
    RenderFrameContext frame_context;
    MainSwapchain main_swapchain {};
    ImGuiTextureRegistry registry;
    ImGuiRenderer renderer;
    renderer.initialize(device, registry);

    ImGui::CreateContext();
    begin_test_frame();
    auto snapshot = capture_imgui_frame_snapshot();
    renderer.render(
        device,
        pipeline_cache,
        frame_context,
        main_swapchain,
        registry,
        *snapshot
    );

    REQUIRE_FALSE(device.texture_update_calls.empty());
    REQUIRE(ImGui::GetPlatformIO().Textures.Size > 0);
    ImTextureData* texture_data = ImGui::GetPlatformIO().Textures[0];
    REQUIRE(texture_data->Format == ImTextureFormat_RGBA32);
    REQUIRE(texture_data->Status == ImTextureStatus_OK);
    REQUIRE(texture_data->GetTexID() != ImTextureID_Invalid);
    const ImTextureID texture_id = texture_data->GetTexID();
    REQUIRE(registry.contains(texture_id));

    REQUIRE(texture_data->Width >= 3);
    REQUIRE(texture_data->Height >= 2);
    begin_test_frame();
    texture_data->UpdateRect = ImTextureRect {.x = 1, .y = 0, .w = 2, .h = 2};
    auto* pixels = static_cast<unsigned char*>(texture_data->GetPixels());
    std::vector<std::byte> expected;
    expected.reserve(16);
    for (int row = 0; row < 2; ++row) {
        const auto source_offset =
            (static_cast<std::ptrdiff_t>(row) * texture_data->Width + 1) * 4;
        auto* source = pixels + source_offset;
        for (int byte = 0; byte < 8; ++byte) {
            source[byte] = static_cast<unsigned char>(row * 16 + byte);
            expected.push_back(static_cast<std::byte>(source[byte]));
        }
    }
    texture_data->SetStatus(ImTextureStatus_WantUpdates);
    snapshot = capture_imgui_frame_snapshot();
    renderer.render(
        device,
        pipeline_cache,
        frame_context,
        main_swapchain,
        registry,
        *snapshot
    );

    const auto& update = device.texture_update_calls.back();
    REQUIRE(update.x == 1);
    REQUIRE(update.y == 0);
    REQUIRE(update.width == 2);
    REQUIRE(update.height == 2);
    REQUIRE(update.bytes == expected);
    REQUIRE(texture_data->Status == ImTextureStatus_OK);

    ImGuiFrameSnapshot destroy_snapshot;
    destroy_snapshot.texture_operations.push_back(
        ImGuiTextureOperation {
            .kind = ImGuiTextureOperationKind::Destroy,
            .texture_id = static_cast<uint64>(texture_id),
        }
    );
    renderer.render(
        device,
        pipeline_cache,
        frame_context,
        main_swapchain,
        registry,
        destroy_snapshot
    );
    REQUIRE_FALSE(registry.contains(texture_id));

    renderer.shutdown(registry);
    ImGui::DestroyContext();
}
