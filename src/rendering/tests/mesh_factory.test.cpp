#include "rendering/mesh/mesh_factory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <cstdint>

using namespace fei;
using Catch::Matchers::WithinAbs;

namespace {

void require_bounds(
    const Mesh& mesh,
    float min_x,
    float min_y,
    float min_z,
    float max_x,
    float max_y,
    float max_z
) {
    const auto bounds = mesh.compute_aabb();
    REQUIRE_THAT(bounds.min.x, WithinAbs(min_x, EPSILON));
    REQUIRE_THAT(bounds.min.y, WithinAbs(min_y, EPSILON));
    REQUIRE_THAT(bounds.min.z, WithinAbs(min_z, EPSILON));
    REQUIRE_THAT(bounds.max.x, WithinAbs(max_x, EPSILON));
    REQUIRE_THAT(bounds.max.y, WithinAbs(max_y, EPSILON));
    REQUIRE_THAT(bounds.max.z, WithinAbs(max_z, EPSILON));
}

void require_common_surface_attributes(const Mesh& mesh) {
    REQUIRE(mesh.primitive() == RenderPrimitive::Triangles);
    REQUIRE(mesh.has_attribute(Mesh::ATTRIBUTE_POSITION.id));
    REQUIRE(mesh.has_attribute(Mesh::ATTRIBUTE_NORMAL.id));
    REQUIRE(mesh.has_attribute(Mesh::ATTRIBUTE_UV_0.id));
}

} // namespace

TEST_CASE(
    "MeshFactory creates deterministic quad mesh data",
    "[rendering][mesh-factory]"
) {
    auto mesh = MeshFactory::create_quad(2.0f, 4.0f);

    require_common_surface_attributes(*mesh);
    REQUIRE(mesh->vertex_count() == 4);
    REQUIRE(mesh->index_buffer_size() == 6 * sizeof(std::uint32_t));
    require_bounds(*mesh, -1.0f, -2.0f, 0.0f, 1.0f, 2.0f, 0.0f);
}

TEST_CASE(
    "MeshFactory creates deterministic cube mesh data",
    "[rendering][mesh-factory]"
) {
    auto mesh = MeshFactory::create_cube(2.0f);

    require_common_surface_attributes(*mesh);
    REQUIRE(mesh->vertex_count() == 24);
    REQUIRE(mesh->index_buffer_size() == 36 * sizeof(std::uint32_t));
    require_bounds(*mesh, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
}

TEST_CASE(
    "MeshFactory creates deterministic subdivided plane mesh data",
    "[rendering][mesh-factory]"
) {
    auto mesh = MeshFactory::create_plane(2.0f, 4.0f, 2);

    require_common_surface_attributes(*mesh);
    REQUIRE(mesh->vertex_count() == 9);
    REQUIRE(
        mesh->index_buffer_size() ==
        std::size_t {2} * 2 * 6 * sizeof(std::uint32_t)
    );
    require_bounds(*mesh, -1.0f, 0.0f, -2.0f, 1.0f, 0.0f, 2.0f);
}
