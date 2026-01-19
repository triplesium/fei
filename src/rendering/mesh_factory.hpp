#pragma once

#include "rendering/mesh.hpp"

#include <memory>

namespace fei {

class MeshFactory {
  public:
    static std::unique_ptr<Mesh> create_cube(float size = 1.0f);
    static std::unique_ptr<Mesh> create_sphere(
        float radius = 0.5f,
        std::uint32_t segments = 32,
        std::uint32_t rings = 16
    );
    static std::unique_ptr<Mesh> create_capsule(
        float radius = 0.5f,
        float length = 1.0f,
        std::uint32_t segments = 32,
        std::uint32_t rings = 8
    );
    static std::unique_ptr<Mesh> create_cylinder(
        float radius = 0.5f,
        float height = 1.0f,
        std::uint32_t segments = 32
    );
    static std::unique_ptr<Mesh> create_plane(
        float width = 1.0f,
        float depth = 1.0f,
        std::uint32_t subdivisions = 1
    );
    static std::unique_ptr<Mesh> create_arrow(
        float length = 1.0f,
        float shaft_radius = 0.05f,
        float head_length = 0.2f,
        float head_radius = 0.1f,
        std::uint32_t segments = 32
    );
    static std::unique_ptr<Mesh>
    create_quad(float width = 1.0f, float height = 1.0f);
};

} // namespace fei
