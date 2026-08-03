#pragma once

#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "pbr/material.hpp"
#include "scene/scene.hpp"

#include <cstddef>
#include <vector>

namespace fei {

struct Gltf {
    Optional<std::size_t> default_scene;
    std::vector<Handle<Scene>> scenes;
    std::vector<Handle<SceneMesh>> meshes;
    std::vector<Handle<StandardMaterial>> materials;
    std::vector<Handle<Image>> textures;
};

} // namespace fei
