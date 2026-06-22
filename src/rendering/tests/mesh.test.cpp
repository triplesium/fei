#include "rendering/mesh/mesh.hpp"

#include "app/app.hpp"
#include "asset/server.hpp"
#include "rendering/mesh/mesh_loader.hpp"
#include "test_graphics_device.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;
using Catch::Matchers::WithinAbs;

namespace {

float read_float(const std::byte* data, std::size_t offset) {
    float value = 0.0f;
    std::memcpy(&value, data + offset, sizeof(float));
    return value;
}

std::uint32_t read_uint32(const std::byte* data, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(std::uint32_t));
    return value;
}

void require_bounds(
    const Aabb& bounds,
    float min_x,
    float min_y,
    float min_z,
    float max_x,
    float max_y,
    float max_z
) {
    REQUIRE_THAT(bounds.min.x, WithinAbs(min_x, EPSILON));
    REQUIRE_THAT(bounds.min.y, WithinAbs(min_y, EPSILON));
    REQUIRE_THAT(bounds.min.z, WithinAbs(min_z, EPSILON));
    REQUIRE_THAT(bounds.max.x, WithinAbs(max_x, EPSILON));
    REQUIRE_THAT(bounds.max.y, WithinAbs(max_y, EPSILON));
    REQUIRE_THAT(bounds.max.z, WithinAbs(max_z, EPSILON));
}

void require_components_near(Vector3 actual, float x, float y, float z) {
    REQUIRE_THAT(actual.x, WithinAbs(x, EPSILON));
    REQUIRE_THAT(actual.y, WithinAbs(y, EPSILON));
    REQUIRE_THAT(actual.z, WithinAbs(z, EPSILON));
}

} // namespace

TEST_CASE(
    "VertexAttributeValues reports format, size, and typed access",
    "[rendering][mesh]"
) {
    VertexAttributeValues positions(
        std::vector<std::array<float, 3>> {
            {1.0f, 2.0f, 3.0f},
            {4.0f, 5.0f, 6.0f},
        }
    );
    VertexAttributeValues colors(
        std::vector<std::array<float, 4>> {
            {1.0f, 0.0f, 0.0f, 1.0f},
        }
    );

    REQUIRE(positions.size() == 2);
    REQUIRE(positions.vertex_format() == VertexFormat::Float3);
    REQUIRE(positions.data() != nullptr);

    auto as_float3 = positions.as_float3();
    REQUIRE(as_float3.has_value());
    REQUIRE((*as_float3)[1][2] == 6.0f);
    REQUIRE_FALSE(colors.as_float3().has_value());
    REQUIRE(colors.as_float4().has_value());
    REQUIRE(colors.vertex_format() == VertexFormat::Float4);

    VertexAttributeValues empty_positions(std::vector<std::array<float, 3>> {});
    REQUIRE(empty_positions.size() == 0);
    REQUIRE(empty_positions.vertex_format() == VertexFormat::Float3);
}

TEST_CASE(
    "Mesh handles empty CPU-side data without out-of-bounds access",
    "[rendering][mesh]"
) {
    Mesh empty_mesh(RenderPrimitive::Triangles);
    empty_mesh.center_positions();
    empty_mesh.compute_smooth_normals();
    require_bounds(
        empty_mesh.compute_aabb(),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    );

    Mesh empty_positions(RenderPrimitive::Triangles);
    empty_positions.insert_attribute(
        Mesh::ATTRIBUTE_POSITION,
        std::vector<std::array<float, 3>> {}
    );
    empty_positions.center_positions();
    empty_positions.compute_smooth_normals();
    require_bounds(
        empty_positions.compute_aabb(),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    );
}

TEST_CASE(
    "MeshLoader splits OBJ vertices by position, texcoord, and normal",
    "[rendering][mesh-loader]"
) {
    const std::string obj = R"(
v 0 0 0
v 1 0 0
v 1 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0.5 0.5
f 1/1 2/2 3/3
f 1/4 3/3 2/2
)";

    App app;
    AssetServer server(&app);
    SyncLoadContext context(server, AssetPath("memory://seam.obj"));
    Reader reader(std::string_view(obj.data(), obj.size()));
    MeshLoader loader;

    auto loaded = loader.load(reader, context);

    REQUIRE(loaded);
    REQUIRE((*loaded)->vertex_count() == 4);
    REQUIRE((*loaded)->index_buffer_size() == 6 * sizeof(std::uint32_t));

    auto bytes = (*loaded)->index_buffer_data();
    const auto* data = bytes.get();
    REQUIRE(read_uint32(data, 0) == 0);
    REQUIRE(read_uint32(data, 4) == 1);
    REQUIRE(read_uint32(data, 8) == 2);
    REQUIRE(read_uint32(data, 12) == 3);
    REQUIRE(read_uint32(data, 16) == 2);
    REQUIRE(read_uint32(data, 20) == 1);
}

TEST_CASE(
    "Mesh packs vertex attributes into an interleaved buffer",
    "[rendering][mesh]"
) {
    Mesh mesh(RenderPrimitive::Triangles);
    mesh.insert_attribute(
        Mesh::ATTRIBUTE_POSITION,
        std::vector<std::array<float, 3>> {
            {1.0f, 2.0f, 3.0f},
            {4.0f, 5.0f, 6.0f},
        }
    );
    mesh.insert_attribute(
        Mesh::ATTRIBUTE_COLOR,
        std::vector<std::array<float, 4>> {
            {0.1f, 0.2f, 0.3f, 0.4f},
            {0.5f, 0.6f, 0.7f, 0.8f},
        }
    );

    REQUIRE(mesh.vertex_count() == 2);
    REQUIRE(mesh.vertex_size() == 28);
    REQUIRE(mesh.vertex_buffer_size() == 56);

    auto layout = mesh.vertex_buffer_layout();
    REQUIRE(layout.attribute_ids.size() == 2);
    REQUIRE(layout.attribute_ids[0] == Mesh::ATTRIBUTE_POSITION.id);
    REQUIRE(layout.attribute_ids[1] == Mesh::ATTRIBUTE_COLOR.id);
    REQUIRE(layout.layout.stride == 28);
    REQUIRE(layout.layout.attributes[0].location == 0);
    REQUIRE(layout.layout.attributes[0].offset == 0);
    REQUIRE(layout.layout.attributes[0].format == VertexFormat::Float3);
    REQUIRE(layout.layout.attributes[1].location == 1);
    REQUIRE(layout.layout.attributes[1].offset == 12);
    REQUIRE(layout.layout.attributes[1].format == VertexFormat::Float4);

    auto bytes = mesh.vertex_buffer_data();
    const auto* data = bytes.get();

    REQUIRE_THAT(read_float(data, 0), WithinAbs(1.0f, EPSILON));
    REQUIRE_THAT(read_float(data, 8), WithinAbs(3.0f, EPSILON));
    REQUIRE_THAT(read_float(data, 12), WithinAbs(0.1f, EPSILON));
    REQUIRE_THAT(read_float(data, 24), WithinAbs(0.4f, EPSILON));
    REQUIRE_THAT(read_float(data, 28), WithinAbs(4.0f, EPSILON));
    REQUIRE_THAT(read_float(data, 40), WithinAbs(0.5f, EPSILON));
    REQUIRE_THAT(read_float(data, 52), WithinAbs(0.8f, EPSILON));
}

TEST_CASE("Mesh packs index data as uint32 values", "[rendering][mesh]") {
    Mesh mesh(RenderPrimitive::Triangles);

    REQUIRE(mesh.index_buffer_size() == 0);
    REQUIRE(mesh.index_buffer_data() == nullptr);

    mesh.insert_indices({2, 1, 0});

    REQUIRE(mesh.index_buffer_size() == 3 * sizeof(std::uint32_t));

    auto bytes = mesh.index_buffer_data();
    const auto* data = bytes.get();
    REQUIRE(read_uint32(data, 0) == 2);
    REQUIRE(read_uint32(data, 4) == 1);
    REQUIRE(read_uint32(data, 8) == 0);
}

TEST_CASE(
    "Mesh computes bounds and transforms positions on the CPU",
    "[rendering][mesh]"
) {
    Mesh mesh(RenderPrimitive::Triangles);
    mesh.insert_attribute(
        Mesh::ATTRIBUTE_POSITION,
        std::vector<std::array<float, 3>> {
            {-2.0f, 1.0f, 3.0f},
            {4.0f, -1.0f, -5.0f},
            {0.0f, 2.0f, 1.0f},
        }
    );

    auto bounds = mesh.compute_aabb();
    require_bounds(bounds, -2.0f, -1.0f, -5.0f, 4.0f, 2.0f, 3.0f);

    mesh.center_positions();
    auto centered_bounds = mesh.compute_aabb();
    require_components_near(centered_bounds.center(), 0.0f, 0.0f, 0.0f);
    require_components_near(centered_bounds.extent(), 3.0f, 1.5f, 4.0f);

    mesh.scale_by(Vector3 {2.0f, 2.0f, 2.0f});
    auto scaled_bounds = mesh.compute_aabb();
    require_bounds(scaled_bounds, -6.0f, -3.0f, -8.0f, 6.0f, 3.0f, 8.0f);
}

TEST_CASE(
    "GpuMeshAdapter creates vertex and index buffers through GraphicsDevice",
    "[rendering][mesh]"
) {
    World world;
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());

    Mesh mesh(RenderPrimitive::Triangles);
    mesh.insert_attribute(
        Mesh::ATTRIBUTE_POSITION,
        std::vector<std::array<float, 3>> {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        }
    );
    mesh.insert_indices({0, 1, 2});

    GpuMeshAdapter adapter;
    auto prepared = adapter.prepare_asset(mesh, world);

    REQUIRE(prepared.has_value());
    REQUIRE(prepared->primitive() == RenderPrimitive::Triangles);
    REQUIRE(prepared->vertex_count() == 3);
    REQUIRE(prepared->index_buffer_size() == 3 * sizeof(std::uint32_t));
    REQUIRE(prepared->vertex_buffer() == device.buffers[0]);
    REQUIRE(prepared->index_buffer().has_value());
    REQUIRE(*prepared->index_buffer() == device.buffers[1]);

    REQUIRE(device.buffer_descriptions.size() == 2);
    REQUIRE(device.buffer_descriptions[0].size == mesh.vertex_buffer_size());
    REQUIRE(device.buffer_descriptions[0].usages.is_set(BufferUsages::Vertex));
    REQUIRE(device.buffer_descriptions[1].size == mesh.index_buffer_size());
    REQUIRE(device.buffer_descriptions[1].usages.is_set(BufferUsages::Index));

    REQUIRE(device.buffer_update_calls.size() == 2);
    REQUIRE(device.buffer_update_calls[0].buffer == prepared->vertex_buffer());
    REQUIRE(device.buffer_update_calls[0].offset == 0);
    REQUIRE(
        device.buffer_update_calls[0].bytes.size() == mesh.vertex_buffer_size()
    );
    REQUIRE(device.buffer_update_calls[1].buffer == *prepared->index_buffer());
    REQUIRE(device.buffer_update_calls[1].offset == 0);
    REQUIRE(
        device.buffer_update_calls[1].bytes.size() == mesh.index_buffer_size()
    );
}
