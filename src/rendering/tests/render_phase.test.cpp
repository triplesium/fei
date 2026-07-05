#include "rendering/render_phase.hpp"

#include "test_graphics_device.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class RecordingCommandBuffer : public CommandBuffer {
  public:
    bool began {false};
    bool ended {false};
    std::shared_ptr<const Pipeline> render_pipeline;
    std::array<std::shared_ptr<const ResourceSet>, 3> resource_sets {};
    std::shared_ptr<const Buffer> vertex_buffer;
    std::shared_ptr<const Buffer> index_buffer;
    IndexFormat index_format {IndexFormat::Uint32};
    uint32 index_offset {0};
    usize draw_start {0};
    usize draw_count {0};
    usize draw_indexed_count {0};

    void begin() override { began = true; }
    void end() override { ended = true; }

    void begin_render_pass(const RenderPassDescription&) override {}
    void end_render_pass() override {}

    void set_viewport(int32, int32, uint32, uint32) override {}

    void set_vertex_buffer(std::shared_ptr<const Buffer> buffer) override {
        vertex_buffer = std::move(buffer);
    }

    void set_resource_set(
        uint32 slot,
        std::shared_ptr<const ResourceSet> resource_set,
        std::span<const uint32>
    ) override {
        resource_sets.at(slot) = std::move(resource_set);
    }

    void
    update_buffer(std::shared_ptr<Buffer>, const void*, std::size_t) override {}

    void draw(std::size_t start, std::size_t count) override {
        draw_start = start;
        draw_count = count;
    }

    void draw_indexed(std::size_t count) override {
        draw_indexed_count = count;
    }

    void dispatch(std::size_t, std::size_t, std::size_t) override {}

  protected:
    void set_render_pipeline_impl(
        std::shared_ptr<const Pipeline> pipeline
    ) override {
        render_pipeline = std::move(pipeline);
    }

    void set_compute_pipeline_impl(std::shared_ptr<const Pipeline>) override {}

    void set_index_buffer_impl(
        std::shared_ptr<const Buffer> buffer,
        IndexFormat format,
        uint32 offset
    ) override {
        index_buffer = std::move(buffer);
        index_format = format;
        index_offset = offset;
    }

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

std::shared_ptr<ResourceSet> make_resource_set() {
    auto layout =
        std::make_shared<ResourceLayout>(ResourceLayoutDescription {});
    return std::make_shared<ResourceSet>(
        ResourceSetDescription {.layout = layout, .resources = {}}
    );
}

MeshVertexBufferLayout make_vertex_layout() {
    return MeshVertexBufferLayout {
        .attribute_ids = {},
        .layout = VertexBufferLayout(VertexStepMode::Vertex, {}),
    };
}

} // namespace

TEST_CASE(
    "RenderPhase clears and sorts mesh draw items by pipeline",
    "[rendering][phase]"
) {
    FakeGraphicsDevice device;
    PipelineCache cache(device);
    auto pipeline_0 =
        cache.request_render_pipeline(RenderPipelineDescription {});
    auto pipeline_1 =
        cache.request_render_pipeline(RenderPipelineDescription {});
    auto pipeline_2 =
        cache.request_render_pipeline(RenderPipelineDescription {});

    RenderPhase<MeshDrawItem> phase;
    phase.items.push_back(MeshDrawItem {.pipeline = pipeline_2});
    phase.items.push_back(MeshDrawItem {.pipeline = pipeline_0});
    phase.items.push_back(MeshDrawItem {.pipeline = pipeline_1});

    sort_by_pipeline(phase);

    REQUIRE(phase.items[0].pipeline == pipeline_0);
    REQUIRE(phase.items[1].pipeline == pipeline_1);
    REQUIRE(phase.items[2].pipeline == pipeline_2);

    phase.clear();

    REQUIRE(phase.items.empty());
}

TEST_CASE(
    "make_mesh_draw_item captures mesh buffers and draw counts",
    "[rendering][phase]"
) {
    auto vertex_buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 48, .usages = BufferUsages::Vertex}
    );
    std::shared_ptr<Buffer> index_buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 12, .usages = BufferUsages::Index}
    );
    GpuMesh gpu_mesh(
        vertex_buffer,
        Optional<std::shared_ptr<Buffer>> {index_buffer},
        RenderPrimitive::Triangles,
        make_vertex_layout(),
        12,
        4
    );

    auto view_set = make_resource_set();
    auto mesh_set = make_resource_set();
    auto material_set = make_resource_set();
    auto item = make_mesh_draw_item(
        42,
        static_cast<CachedRenderPipelineId>(7),
        view_set,
        mesh_set,
        material_set,
        gpu_mesh,
        3.5f
    );

    REQUIRE(item.entity == 42);
    REQUIRE(item.pipeline == static_cast<CachedRenderPipelineId>(7));
    REQUIRE(item.view_set == view_set);
    REQUIRE(item.mesh_set == mesh_set);
    REQUIRE(item.material_set == material_set);
    REQUIRE(item.vertex_buffer == vertex_buffer);
    REQUIRE(item.index_buffer == index_buffer);
    REQUIRE(item.index_count == 3);
    REQUIRE(item.vertex_count == 4);
    REQUIRE(item.depth == 3.5f);
}

TEST_CASE(
    "draw_mesh_item records indexed mesh draw state",
    "[rendering][phase]"
) {
    FakeGraphicsDevice device;
    PipelineCache cache(device);
    auto pipeline_id =
        cache.request_render_pipeline(RenderPipelineDescription {});
    cache.process_queued_pipelines();

    auto view_set = make_resource_set();
    auto mesh_set = make_resource_set();
    auto material_set = make_resource_set();
    auto vertex_buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 48, .usages = BufferUsages::Vertex}
    );
    auto index_buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 12, .usages = BufferUsages::Index}
    );
    MeshDrawItem item {
        .pipeline = pipeline_id,
        .view_set = view_set,
        .mesh_set = mesh_set,
        .material_set = material_set,
        .vertex_buffer = vertex_buffer,
        .index_buffer = index_buffer,
        .index_count = 3,
        .vertex_count = 4,
    };
    RecordingCommandBuffer command_buffer;

    draw_mesh_item(command_buffer, cache, item);

    REQUIRE(command_buffer.render_pipeline == device.render_pipelines[0]);
    REQUIRE(command_buffer.resource_sets[0] == view_set);
    REQUIRE(command_buffer.resource_sets[1] == mesh_set);
    REQUIRE(command_buffer.resource_sets[2] == material_set);
    REQUIRE(command_buffer.vertex_buffer == vertex_buffer);
    REQUIRE(command_buffer.index_buffer == index_buffer);
    REQUIRE(command_buffer.index_format == IndexFormat::Uint32);
    REQUIRE(command_buffer.index_offset == 0);
    REQUIRE(command_buffer.draw_indexed_count == 3);
    REQUIRE(command_buffer.draw_count == 0);
}

TEST_CASE(
    "draw_mesh_item records non-indexed mesh draw state",
    "[rendering][phase]"
) {
    FakeGraphicsDevice device;
    PipelineCache cache(device);
    auto pipeline_id =
        cache.request_render_pipeline(RenderPipelineDescription {});
    cache.process_queued_pipelines();

    auto vertex_buffer = std::make_shared<FakeBuffer>(
        BufferDescription {.size = 48, .usages = BufferUsages::Vertex}
    );
    MeshDrawItem item {
        .pipeline = pipeline_id,
        .view_set = make_resource_set(),
        .mesh_set = make_resource_set(),
        .material_set = make_resource_set(),
        .vertex_buffer = vertex_buffer,
        .vertex_count = 4,
    };
    RecordingCommandBuffer command_buffer;

    draw_mesh_item(command_buffer, cache, item);

    REQUIRE(command_buffer.render_pipeline == device.render_pipelines[0]);
    REQUIRE(command_buffer.vertex_buffer == vertex_buffer);
    REQUIRE(command_buffer.index_buffer == nullptr);
    REQUIRE(command_buffer.draw_start == 0);
    REQUIRE(command_buffer.draw_count == 4);
    REQUIRE(command_buffer.draw_indexed_count == 0);
}

TEST_CASE(
    "draw_mesh_item skips missing render pipelines",
    "[rendering][phase]"
) {
    FakeGraphicsDevice device;
    PipelineCache cache(device);
    MeshDrawItem item {
        .pipeline = static_cast<CachedRenderPipelineId>(99),
        .view_set = make_resource_set(),
        .mesh_set = make_resource_set(),
        .material_set = make_resource_set(),
        .vertex_buffer = std::make_shared<FakeBuffer>(
            BufferDescription {.size = 48, .usages = BufferUsages::Vertex}
        ),
        .vertex_count = 4,
    };
    RecordingCommandBuffer command_buffer;

    draw_mesh_item(command_buffer, cache, item);

    REQUIRE(command_buffer.render_pipeline == nullptr);
    REQUIRE(command_buffer.vertex_buffer == nullptr);
    REQUIRE(command_buffer.draw_count == 0);
    REQUIRE(command_buffer.draw_indexed_count == 0);
}
