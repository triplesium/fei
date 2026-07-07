#pragma once
#include "graphics/shader_module.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fei {

struct ShaderArtifactLogicalResourceName {
    std::string name;
    uint32_t set {0};
    uint32_t binding {0};
};

struct ShaderArtifactGenerationInput {
    std::vector<std::byte> spirv;
    std::vector<ShaderArtifactLogicalResourceName> logical_resource_names;
};

struct ShaderArtifactGenerationOutput {
    std::string opengl_source;
    std::vector<ShaderResourceBinding> resources;
};

ShaderArtifactGenerationOutput
generate_shader_artifacts(const ShaderArtifactGenerationInput& input);

} // namespace fei
