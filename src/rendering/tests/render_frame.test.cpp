#include "rendering/render_frame.hpp"

#include "app/app.hpp"
#include "rendering/render_queue.hpp"
#include "rendering/resource_set_cache.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class LifecycleCommandBuffer : public CommandBuffer {
  public:
    struct BufferUpdate {
        std::shared_ptr<Buffer> buffer;
        uint32 offset {};
        std::vector<std::byte> data;
    };

    uint32 begin_calls {0};
    uint32 end_calls {0};
    std::vector<BufferUpdate> buffer_updates;

    void begin() override { ++begin_calls; }
    void end() override { ++end_calls; }
    void begin_render_pass(const RenderPassDescription&) override {}
    void end_render_pass() override {}
    void set_viewport(int32, int32, uint32, uint32) override {}
    void set_vertex_buffer(std::shared_ptr<const Buffer>) override {}
    void set_resource_set(
        uint32,
        std::shared_ptr<const ResourceSet>,
        std::span<const uint32>
    ) override {}
    void update_buffer(
        std::shared_ptr<Buffer> buffer,
        uint32 offset,
        const void* data,
        std::size_t size
    ) override {
        const auto* bytes = static_cast<const std::byte*>(data);
        buffer_updates.push_back(
            BufferUpdate {
                .buffer = std::move(buffer),
                .offset = offset,
                .data = std::vector<std::byte>(bytes, bytes + size),
            }
        );
    }
    void draw(std::size_t, std::size_t) override {}
    void draw_indexed(std::size_t) override {}
    void dispatch(std::size_t, std::size_t, std::size_t) override {}

  protected:
    void set_render_pipeline_impl(std::shared_ptr<const Pipeline>) override {}
    void set_compute_pipeline_impl(std::shared_ptr<const Pipeline>) override {}
    void set_index_buffer_impl(
        std::shared_ptr<const Buffer>,
        IndexFormat,
        uint32
    ) override {}
    void generate_mipmaps_impl(std::shared_ptr<const Texture>) override {}
    void copy_texture_impl(
        std::shared_ptr<const Texture>,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        std::shared_ptr<const Texture>,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32
    ) override {}
};

} // namespace

TEST_CASE(
    "RenderFrameContext owns one command buffer lifecycle",
    "[rendering][frame]"
) {
    FakeGraphicsDevice device;
    auto commands = std::make_shared<LifecycleCommandBuffer>();
    device.next_command_buffer = commands;
    RenderFrameContext frame;

    REQUIRE(frame.begin(device));
    REQUIRE(frame.command_buffer() == commands.get());
    REQUIRE(commands->begin_calls == 1);
    REQUIRE_FALSE(frame.begin(device));
    REQUIRE(commands->begin_calls == 1);

    auto finished = frame.finish();
    REQUIRE(finished == commands);
    REQUIRE(commands->end_calls == 1);
    REQUIRE(frame.command_buffer() == nullptr);
    REQUIRE(frame.finish() == nullptr);
    REQUIRE(commands->end_calls == 1);
}

TEST_CASE(
    "RenderQueue copies writes and flushes after frame begin",
    "[rendering][frame][queue]"
) {
    constexpr ScheduleId TestRenderSchedule = 78;
    App app;
    app.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    app.add_resource(RenderFrameContext {});
    app.add_resource(RenderQueue {});

    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(app.resource<GraphicsDevice>());
    auto commands = std::make_shared<LifecycleCommandBuffer>();
    device.next_command_buffer = commands;
    auto buffer = device.create_buffer(
        BufferDescription {.size = 16, .usages = BufferUsages::Uniform}
    );

    uint32 value = 0x12345678;
    app.resource<RenderQueue>().write_buffer(buffer, 4, &value, sizeof(value));
    value = 0;
    REQUIRE(app.resource<RenderQueue>().pending_buffer_writes() == 1);

    app.add_systems(
        TestRenderSchedule,
        chain(begin_render_frame, flush_render_queue)
    );
    app.world().sort_systems();
    app.run_schedule(TestRenderSchedule);

    REQUIRE(app.resource<RenderQueue>().pending_buffer_writes() == 0);
    REQUIRE(commands->buffer_updates.size() == 1);
    CHECK(commands->buffer_updates[0].buffer == buffer);
    CHECK(commands->buffer_updates[0].offset == 4);
    uint32 uploaded {};
    std::memcpy(
        &uploaded,
        commands->buffer_updates[0].data.data(),
        sizeof(uploaded)
    );
    CHECK(uploaded == 0x12345678);
}

TEST_CASE(
    "RenderQueue retains writes while the frame has no command buffer",
    "[rendering][frame][queue]"
) {
    App app;
    app.add_resource(RenderFrameContext {});
    app.add_resource(RenderQueue {});
    auto buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 16, .usages = BufferUsages::Uniform}
    );
    uint32 value = 7;
    app.resource<RenderQueue>().write_buffer(buffer, value);

    app.world().run_system_once(flush_render_queue);

    CHECK(app.resource<RenderQueue>().pending_buffer_writes() == 1);
}

TEST_CASE(
    "RenderQueue accepts concurrent writes through read-only resource access",
    "[rendering][frame][queue]"
) {
    RenderQueue queue;
    auto buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 16, .usages = BufferUsages::Uniform}
    );
    std::vector<std::jthread> writers;
    for (uint32 writer = 0; writer < 4; ++writer) {
        writers.emplace_back([queue, buffer, writer] {
            for (uint32 write = 0; write < 25; ++write) {
                const uint32 value = writer * 25 + write;
                queue.write_buffer(buffer, value);
            }
        });
    }
    writers.clear();

    CHECK(queue.pending_buffer_writes() == 100);
}

TEST_CASE(
    "RenderFrameContext safely skips a null command buffer",
    "[rendering][frame]"
) {
    FakeGraphicsDevice device;
    RenderFrameContext frame;

    REQUIRE_FALSE(frame.begin(device));
    REQUIRE_FALSE(frame.recording());
    REQUIRE(frame.finish() == nullptr);
}

TEST_CASE(
    "Render frame systems submit a finished command buffer only once",
    "[rendering][frame]"
) {
    constexpr ScheduleId TestRenderSchedule = 77;
    App app;
    app.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    app.add_resource(RenderFrameContext {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(app.resource<GraphicsDevice>());
    auto commands = std::make_shared<LifecycleCommandBuffer>();
    device.next_command_buffer = commands;
    app.add_systems(
        TestRenderSchedule,
        chain(begin_render_frame, submit_render_frame)
    );

    app.world().sort_systems();
    app.run_schedule(TestRenderSchedule);
    REQUIRE(device.submitted_commands.size() == 1);
    REQUIRE(device.submitted_commands[0] == commands);
    REQUIRE(commands->begin_calls == 1);
    REQUIRE(commands->end_calls == 1);

    app.world().run_system_once(submit_render_frame);
    REQUIRE(device.submitted_commands.size() == 1);
    REQUIRE(commands->end_calls == 1);
}

TEST_CASE(
    "RenderResourceSetCache keys physical resources and buffer ranges",
    "[rendering][resource-set-cache]"
) {
    FakeGraphicsDevice device;
    RenderResourceSetCache cache;
    auto layout = device.create_resource_layout(ResourceLayoutDescription {});
    auto buffer = device.create_buffer(
        BufferDescription {.size = 64, .usages = BufferUsages::Uniform}
    );
    auto first_range = std::make_shared<BufferRange>(buffer, 0, 16);
    auto same_range = std::make_shared<BufferRange>(buffer, 0, 16);
    auto other_range = std::make_shared<BufferRange>(buffer, 16, 16);

    auto first = cache.get_or_create(device, "first", layout, {first_range});
    auto hit = cache.get_or_create(device, "same", layout, {same_range});
    auto different =
        cache.get_or_create(device, "different", layout, {other_range});

    REQUIRE(first == hit);
    REQUIRE(first != different);
    REQUIRE(device.resource_set_descriptions.size() == 2);
    REQUIRE(cache.stats().requests == 3);
    REQUIRE(cache.stats().hits == 1);
    REQUIRE(cache.stats().creates == 2);

    auto first_texture = device.create_texture(
        TextureDescription {
            .width = 16,
            .height = 16,
            .depth = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage = TextureUsage::Sampled,
            .texture_type = TextureType::Texture2D,
        }
    );
    auto rebuilt_texture = device.create_texture(
        TextureDescription {
            .width = 16,
            .height = 16,
            .depth = 1,
            .texture_format = PixelFormat::Rgba8Unorm,
            .texture_usage = TextureUsage::Sampled,
            .texture_type = TextureType::Texture2D,
        }
    );
    auto texture_set =
        cache.get_or_create(device, "texture", layout, {first_texture});
    auto rebuilt_set =
        cache.get_or_create(device, "rebuilt", layout, {rebuilt_texture});
    REQUIRE(texture_set != rebuilt_set);

    for (uint32 frame = 0; frame < 121; ++frame) {
        cache.begin_frame();
    }
    REQUIRE(cache.stats().size == 0);
}
