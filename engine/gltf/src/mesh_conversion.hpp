#pragma once

#include "base/result.hpp"
#include "rendering/mesh/mesh.hpp"

#include <fastgltf/types.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fei::gltf_detail {

struct ConvertedPrimitive {
    std::unique_ptr<Mesh> mesh;
    std::optional<std::size_t> material_index;
};

struct ConvertedMesh {
    std::string name;
    std::vector<ConvertedPrimitive> primitives;
};

Result<std::vector<ConvertedMesh>, std::string>
convert_meshes(const fastgltf::Asset& asset);

} // namespace fei::gltf_detail
