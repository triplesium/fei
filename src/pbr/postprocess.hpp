#pragma once

#include "asset/handle.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_factory.hpp"

namespace fei {

struct FullscreenQuad {
    Handle<Mesh> fullscreen_quad_mesh;
};

inline void
setup_fullscreen_quad(Res<Assets<Mesh>> mesh_assets, Commands commands) {
    auto quad_mesh_handle =
        mesh_assets->add(MeshFactory::create_quad(2.0f, 2.0f));
    auto quad_mesh = mesh_assets->get(quad_mesh_handle).value();
    commands.add_resource(FullscreenQuad {
        quad_mesh_handle,
    });
}

} // namespace fei
