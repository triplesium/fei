#pragma once
#include "asset/handle.hpp"
#include "graphics/pipeline.hpp"
#include "rendering/material.hpp"
#include "rendering/mesh.hpp"

#include <memory>

namespace fei {

struct MeshRenderable {
    Handle<Mesh> mesh;
    Handle<Material> material;
    std::shared_ptr<Pipeline> pipeline;
};

struct Mesh3d {
    Handle<Mesh> mesh;
    bool cast_shadow {true};
};

template<std::derived_from<Material> T>
struct MeshMaterial3d {
    Handle<T> material;
};

} // namespace fei
