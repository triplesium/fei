#include "rendering/shader.hpp"

#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "graphics/resource.hpp"

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {

namespace {

std::filesystem::path shader_output_root() {
#ifdef FEI_SHADER_ASSETS_PATH
    return std::filesystem::path(FEI_SHADER_ASSETS_PATH);
#else
    return std::filesystem::current_path() / "build" / "generated" / "shaders";
#endif
}

std::filesystem::path shader_output_path(
    const std::filesystem::path& logical_path,
    const std::filesystem::path& subdir,
    std::string_view suffix = {}
) {
    auto output_path = shader_output_root() / subdir / logical_path;
    output_path += suffix;
    return output_path;
}

Optional<std::filesystem::path> existing_shader_output_path(
    const std::filesystem::path& logical_path,
    const std::filesystem::path& subdir,
    std::string_view suffix = {}
) {
    auto output_path = shader_output_path(logical_path, subdir, suffix);
    if (!std::filesystem::exists(output_path)) {
        return nullopt;
    }
    return output_path;
}

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

struct ShaderArtifactManifest {
    std::filesystem::path logical_path;
    std::filesystem::path opengl_path;
    std::filesystem::path spirv_path;
    std::filesystem::path reflection_path;
};

// NOTE: Shader assets currently resolve one logical path to several generated
// artifacts. AssetLoader only accepts a Reader, so this manifest is a narrow
// bridge across that API boundary. Replace it when the asset system can return
// structured artifact metadata or a single packaged shader asset.
std::string serialize_shader_manifest(
    const std::filesystem::path& logical_path,
    const std::filesystem::path& opengl_path,
    const std::filesystem::path& spirv_path,
    const std::filesystem::path& reflection_path
) {
    return "format=fei.shader.v1\nlogical=" + logical_path.generic_string() +
           "\nopengl=" + opengl_path.generic_string() +
           "\nspirv=" + spirv_path.generic_string() +
           "\nreflection=" + reflection_path.generic_string() + "\n";
}

Optional<std::string>
manifest_field(std::string_view manifest, std::string_view name) {
    std::string key = std::string(name) + "=";
    std::size_t line_start = 0;
    while (line_start <= manifest.size()) {
        auto line_end = manifest.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = manifest.size();
        }
        auto line = manifest.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.starts_with(key)) {
            return std::string(line.substr(key.size()));
        }
        if (line_end == manifest.size()) {
            break;
        }
        line_start = line_end + 1;
    }
    return nullopt;
}

AssetLoadError
shader_load_error(const AssetPath& asset_path, std::string message) {
    return AssetLoadError(asset_path, std::move(message));
}

AssetLoadError
shader_load_error(const LoadContext& context, std::string message) {
    return shader_load_error(context.asset_path(), std::move(message));
}

Result<Reader, AssetLoadError> read_shader_text_file(
    const AssetPath& asset_path,
    const std::filesystem::path& path
) {
    auto reader = Reader::from_file(path);
    if (!reader) {
        return failure(shader_load_error(asset_path, reader.error().message));
    }
    return std::move(*reader);
}

Result<Reader, AssetLoadError> read_shader_text_file(
    const LoadContext& context,
    const std::filesystem::path& path
) {
    return read_shader_text_file(context.asset_path(), path);
}

Status<AssetLoadError> require_shader_artifact(
    const LoadContext& context,
    const std::filesystem::path& path,
    std::string_view description
) {
    if (std::filesystem::exists(path)) {
        return {};
    }
    return failure(shader_load_error(
        context,
        std::string("Generated ") + std::string(description) +
            " is missing: " + path.string()
    ));
}

struct ShaderArtifactFile {
    std::string_view description;
    const std::filesystem::path& path;
};

Status<AssetLoadError> require_shader_artifacts(
    const LoadContext& context,
    const ShaderArtifactManifest& manifest
) {
    const ShaderArtifactFile artifacts[] = {
        {"OpenGL shader", manifest.opengl_path},
        {"SPIR-V shader", manifest.spirv_path},
        {"shader reflection", manifest.reflection_path},
    };
    for (const auto& artifact : artifacts) {
        auto status = require_shader_artifact(
            context,
            artifact.path,
            artifact.description
        );
        if (!status) {
            return failure(std::move(status).error());
        }
    }
    return {};
}

Result<ShaderArtifactManifest, AssetLoadError>
parse_shader_manifest(std::string_view manifest, const AssetPath& asset_path) {
    auto format = manifest_field(manifest, "format");
    auto logical_path = manifest_field(manifest, "logical");
    auto opengl_path = manifest_field(manifest, "opengl");
    auto spirv_path = manifest_field(manifest, "spirv");
    auto reflection_path = manifest_field(manifest, "reflection");
    if (!format || *format != "fei.shader.v1" || !logical_path ||
        !opengl_path || !spirv_path || !reflection_path) {
        return failure(shader_load_error(
            asset_path,
            "Invalid shader manifest: expected format=fei.shader.v1 with "
            "logical, opengl, spirv, and reflection fields"
        ));
    }
    return ShaderArtifactManifest {
        .logical_path = *logical_path,
        .opengl_path = *opengl_path,
        .spirv_path = *spirv_path,
        .reflection_path = *reflection_path,
    };
}

using ReflectionJson = nlohmann::json;

Result<ReflectionJson, std::string>
parse_shader_reflection_json(std::string_view json) {
    try {
        auto document = ReflectionJson::parse(json);
        if (!document.is_object()) {
            return failure(
                std::string("Shader reflection root is not a JSON object")
            );
        }
        return document;
    } catch (const ReflectionJson::exception& e) {
        return failure(
            std::string("Invalid shader reflection JSON: ") + e.what()
        );
    }
}

uint32 shader_resource_array_size(const ReflectionJson& object) {
    auto array = object.find("array");
    if (array == object.end() || !array->is_array() || array->empty()) {
        return 1;
    }

    auto value = array->front().get<uint32>();
    return value == 0 ? 1 : value;
}

Status<std::string> append_shader_reflection_bindings(
    std::vector<ShaderResourceBinding>& bindings,
    const ReflectionJson& document,
    std::string_view array_name,
    ResourceKind kind
) {
    auto array = document.find(array_name);
    if (array == document.end()) {
        return {};
    }
    if (!array->is_array()) {
        return failure(
            "Shader reflection field '" + std::string(array_name) +
            "' is not an array"
        );
    }

    for (const auto& object : *array) {
        if (!object.is_object()) {
            return failure(
                "Shader reflection field '" + std::string(array_name) +
                "' contains a non-object entry"
            );
        }

        auto name = object.find("name");
        auto set = object.find("set");
        auto binding = object.find("binding");
        if (name == object.end() || set == object.end() ||
            binding == object.end()) {
            continue;
        }

        bindings.push_back(
            ShaderResourceBinding {
                .name = name->get<std::string>(),
                .kind = kind,
                .set = set->get<uint32>(),
                .binding = binding->get<uint32>(),
                .array_size = shader_resource_array_size(object),
            }
        );
    }
    return {};
}

struct ShaderReflectionResourceArray {
    std::string_view name;
    ResourceKind kind;
};

constexpr ShaderReflectionResourceArray shader_reflection_resource_arrays[] = {
    {"ubos", ResourceKind::UniformBuffer},
    {"ssbos", ResourceKind::StorageBufferReadWrite},
    {"textures", ResourceKind::TextureReadOnly},
    {"sampled_images", ResourceKind::TextureReadOnly},
    {"separate_images", ResourceKind::TextureReadOnly},
    {"images", ResourceKind::TextureReadWrite},
    {"storage_images", ResourceKind::TextureReadWrite},
    {"separate_samplers", ResourceKind::Sampler},
};

} // namespace

Optional<std::filesystem::path>
compiled_opengl_shader_path(const AssetPath& asset_path) {
    return existing_shader_output_path(asset_path.path(), "opengl");
}

Optional<std::filesystem::path>
compiled_vulkan_shader_path(const AssetPath& asset_path) {
    return existing_shader_output_path(asset_path.path(), "vulkan", ".spv");
}

Optional<std::filesystem::path>
shader_reflection_path(const AssetPath& asset_path) {
    return existing_shader_output_path(
        asset_path.path(),
        "reflection",
        ".json"
    );
}

Result<std::vector<std::byte>, ReaderError>
read_shader_binary(const std::filesystem::path& path) {
    auto reader = Reader::from_file(path);
    if (!reader) {
        return failure(std::move(reader).error());
    }
    return std::vector<std::byte>(
        reader->data(),
        reader->data() + reader->size()
    );
}

ShaderReflectionResult parse_shader_reflection_bindings(std::string_view json) {
    try {
        auto document = parse_shader_reflection_json(json);
        if (!document) {
            return failure(std::move(document).error());
        }

        std::vector<ShaderResourceBinding> bindings;
        for (const auto& resource_array : shader_reflection_resource_arrays) {
            auto status = append_shader_reflection_bindings(
                bindings,
                *document,
                resource_array.name,
                resource_array.kind
            );
            if (!status) {
                return failure(std::move(status).error());
            }
        }
        return bindings;
    } catch (const ReflectionJson::exception& e) {
        return failure(
            std::string("Invalid shader reflection JSON: ") + e.what()
        );
    }
}

Result<std::vector<ShaderResourceBinding>, AssetLoadError>
load_shader_reflection_bindings(const AssetPath& asset_path) {
    auto reflection_path = shader_reflection_path(asset_path);
    if (!reflection_path) {
        return failure(shader_load_error(
            asset_path,
            "Generated shader reflection is missing: " +
                shader_output_path(asset_path.path(), "reflection", ".json")
                    .string()
        ));
    }
    auto reflection_reader =
        read_shader_text_file(asset_path, reflection_path.value());
    if (!reflection_reader) {
        return failure(std::move(reflection_reader).error());
    }
    auto bindings =
        parse_shader_reflection_bindings(reflection_reader->as_string());
    if (!bindings) {
        return failure(shader_load_error(asset_path, bindings.error()));
    }
    return std::move(bindings).value();
}

AssetLoadResult<Shader>
ShaderLoader::load(Reader& reader, const LoadContext& context) {
    auto manifest =
        parse_shader_manifest(reader.as_string_view(), context.asset_path());
    if (!manifest) {
        return failure(std::move(manifest).error());
    }

    auto stage = shader_stage_from_path(manifest->logical_path);
    if (!stage) {
        return failure(shader_load_error(
            context,
            "Unknown shader extension: " + manifest->logical_path.string()
        ));
    }
    if (auto status = require_shader_artifacts(context, *manifest); !status) {
        return failure(std::move(status).error());
    }

    auto source_reader = read_shader_text_file(context, manifest->opengl_path);
    if (!source_reader) {
        return failure(std::move(source_reader).error());
    }
    std::string source = source_reader->as_string();
    auto spirv = read_shader_binary(manifest->spirv_path);
    if (!spirv) {
        return failure(shader_load_error(context, spirv.error().message));
    }
    auto reflection_reader =
        read_shader_text_file(context, manifest->reflection_path);
    if (!reflection_reader) {
        return failure(std::move(reflection_reader).error());
    }
    auto resources =
        parse_shader_reflection_bindings(reflection_reader->as_string());
    if (!resources) {
        return failure(shader_load_error(context, resources.error()));
    }

    return std::make_unique<Shader>(Shader {
        .path = manifest->logical_path,
        .source = std::move(source),
        .spirv = std::move(*spirv),
        .stage = stage.value(),
        .resources = std::move(resources).value(),
    });
}

ShaderAssetSource::ShaderAssetSource() : m_root(shader_output_root()) {}

ShaderAssetSource::ShaderAssetSource(std::filesystem::path root) :
    m_root(std::move(root)) {}

std::string ShaderAssetSource::name() const {
    return "shader";
}

bool ShaderAssetSource::exists(const std::filesystem::path& path) const {
    return shader_stage_from_path(path).has_value() &&
           std::filesystem::exists(m_root / "opengl" / path) &&
           std::filesystem::exists(
               m_root / "vulkan" / (path.string() + ".spv")
           ) &&
           std::filesystem::exists(
               m_root / "reflection" / (path.string() + ".json")
           );
}

Result<Reader, std::string>
ShaderAssetSource::try_get_reader(const std::filesystem::path& path) const {
    if (!exists(path)) {
        return failure(
            "Generated shader output is missing for '" + path.string() +
            "'. Build the shader owner target, for example "
            "`xmake build -y fei-pbr`."
        );
    }
    auto opengl_path = m_root / "opengl" / path;
    auto spirv_path = m_root / "vulkan" / (path.string() + ".spv");
    auto reflection_path = m_root / "reflection" / (path.string() + ".json");
    auto manifest = serialize_shader_manifest(
        path,
        opengl_path,
        spirv_path,
        reflection_path
    );
    return Reader(std::string_view(manifest));
}

} // namespace fei
