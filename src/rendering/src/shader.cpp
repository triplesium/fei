#include "rendering/shader.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace fei {

namespace {

std::filesystem::path shader_source_root() {
#ifdef FEI_SHADER_SOURCE_PATH
    return std::filesystem::path(FEI_SHADER_SOURCE_PATH);
#else
    return std::filesystem::current_path() / "src" / "pbr" / "shaders";
#endif
}

AssetLoadError
shader_load_error(const AssetPath& asset_path, std::string message) {
    return AssetLoadError(asset_path, std::move(message));
}

AssetLoadError
shader_load_error(const LoadContext& context, std::string message) {
    return shader_load_error(context.asset_path(), std::move(message));
}

Optional<std::filesystem::path> resolve_shader_source_path(
    const std::filesystem::path& root,
    const std::filesystem::path& path
) {
    if (path.extension() == ".slang") {
        auto source_path = root / path;
        if (std::filesystem::exists(source_path)) {
            return source_path.lexically_normal();
        }
        return nullopt;
    }

    if (!shader_stage_from_path(path)) {
        return nullopt;
    }

    auto stage_specific_source = path;
    stage_specific_source += ".slang";
    auto shared_source = path;
    shared_source.replace_extension(".slang");
    for (const auto& candidate : {stage_specific_source, shared_source}) {
        auto source_path = root / candidate;
        if (std::filesystem::exists(source_path)) {
            return source_path.lexically_normal();
        }
    }
    return nullopt;
}

} // namespace

Optional<ShaderStages>
shader_stage_from_path(const std::filesystem::path& path) {
    if (path.extension() == ".vert") {
        return ShaderStages::Vertex;
    }
    if (path.extension() == ".geom") {
        return ShaderStages::Geometry;
    }
    if (path.extension() == ".frag") {
        return ShaderStages::Fragment;
    }
    if (path.extension() == ".comp") {
        return ShaderStages::Compute;
    }
    return nullopt;
}

AssetLoadResult<Shader>
ShaderLoader::load(Reader& reader, const LoadContext& context) {
    auto logical_path = context.asset_path().path();
    auto stage = shader_stage_from_path(logical_path);
    if (logical_path.extension() != ".slang" && !stage) {
        return failure(shader_load_error(
            context,
            "Unknown shader extension: " + logical_path.string()
        ));
    }

    return std::make_unique<Shader>(Shader {
        .path = std::move(logical_path),
        .source = reader.as_string(),
    });
}

ShaderAssetSource::ShaderAssetSource() : m_root(shader_source_root()) {}

ShaderAssetSource::ShaderAssetSource(std::filesystem::path root) :
    m_root(std::move(root)) {}

std::string ShaderAssetSource::name() const {
    return "shader";
}

bool ShaderAssetSource::exists(const std::filesystem::path& path) const {
    return resolve_shader_source_path(m_root, path).has_value();
}

Result<Reader, std::string>
ShaderAssetSource::try_get_reader(const std::filesystem::path& path) const {
    auto source_path = resolve_shader_source_path(m_root, path);
    if (!source_path) {
        return failure(
            "Runtime Slang shader source is missing for '" + path.string() +
            "' under " + m_root.string()
        );
    }

    auto reader = Reader::from_file(source_path.value());
    if (!reader) {
        return failure(std::move(reader).error().message);
    }
    return std::move(reader).value();
}

} // namespace fei
