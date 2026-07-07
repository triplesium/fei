#pragma once

#include <filesystem>

namespace fei::shadergen {

struct ShaderArtifactGenerationRequest {
    std::filesystem::path spirv_path;
    std::filesystem::path opengl_path;
    std::filesystem::path reflection_path;
    std::filesystem::path slang_reflection_path;
};

void generate_shader_artifacts(const ShaderArtifactGenerationRequest& request);

} // namespace fei::shadergen
