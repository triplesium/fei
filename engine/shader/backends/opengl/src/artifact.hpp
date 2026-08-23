#pragma once
#include "shader/compiler.hpp"

#include <string>

namespace ets {

ShaderArtifactGenerationOutput
generate_opengl_shader_artifacts(const ShaderArtifactGenerationInput& input);

std::string opengl_shader_artifact_cache_identity();

} // namespace ets
