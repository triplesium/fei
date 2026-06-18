#include "rendering/shader.hpp"

#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/optional.hpp"
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

ShaderArtifactManifest
parse_shader_manifest(std::string_view manifest, std::string_view asset_path) {
    auto format = manifest_field(manifest, "format");
    auto logical_path = manifest_field(manifest, "logical");
    auto opengl_path = manifest_field(manifest, "opengl");
    auto spirv_path = manifest_field(manifest, "spirv");
    auto reflection_path = manifest_field(manifest, "reflection");
    if (!format || *format != "fei.shader.v1" || !logical_path ||
        !opengl_path || !spirv_path || !reflection_path) {
        fatal("Invalid shader manifest for '{}'", asset_path);
    }
    return ShaderArtifactManifest {
        .logical_path = *logical_path,
        .opengl_path = *opengl_path,
        .spirv_path = *spirv_path,
        .reflection_path = *reflection_path,
    };
}

using ReflectionJson = nlohmann::json;

ReflectionJson parse_shader_reflection_json(std::string_view json) {
    try {
        auto document = ReflectionJson::parse(json);
        if (!document.is_object()) {
            fatal("Shader reflection root is not a JSON object");
        }
        return document;
    } catch (const ReflectionJson::exception& e) {
        fatal("Invalid shader reflection JSON: {}", e.what());
    }
    return {};
}

uint32 shader_resource_array_size(const ReflectionJson& object) {
    auto array = object.find("array");
    if (array == object.end() || !array->is_array() || array->empty()) {
        return 1;
    }

    auto value = array->front().get<uint32>();
    return value == 0 ? 1 : value;
}

void append_shader_reflection_bindings(
    std::vector<ShaderResourceBinding>& bindings,
    const ReflectionJson& document,
    std::string_view array_name,
    ResourceKind kind
) {
    auto array = document.find(array_name);
    if (array == document.end()) {
        return;
    }
    if (!array->is_array()) {
        fatal("Shader reflection field '{}' is not an array", array_name);
    }

    for (const auto& object : *array) {
        if (!object.is_object()) {
            fatal(
                "Shader reflection field '{}' contains a non-object entry",
                array_name
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
}

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

std::vector<std::byte> read_shader_binary(const std::filesystem::path& path) {
    Reader reader(path);
    return std::vector<std::byte>(reader.data(), reader.data() + reader.size());
}

std::vector<ShaderResourceBinding>
parse_shader_reflection_bindings(std::string_view json) {
    try {
        auto document = parse_shader_reflection_json(json);
        std::vector<ShaderResourceBinding> bindings;
        append_shader_reflection_bindings(
            bindings,
            document,
            "ubos",
            ResourceKind::UniformBuffer
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "ssbos",
            ResourceKind::StorageBufferReadWrite
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "textures",
            ResourceKind::TextureReadOnly
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "sampled_images",
            ResourceKind::TextureReadOnly
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "separate_images",
            ResourceKind::TextureReadOnly
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "images",
            ResourceKind::TextureReadWrite
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "storage_images",
            ResourceKind::TextureReadWrite
        );
        append_shader_reflection_bindings(
            bindings,
            document,
            "separate_samplers",
            ResourceKind::Sampler
        );
        return bindings;
    } catch (const ReflectionJson::exception& e) {
        fatal("Invalid shader reflection JSON: {}", e.what());
    }
    return {};
}

std::vector<ShaderResourceBinding>
load_shader_reflection_bindings(const AssetPath& asset_path) {
    auto reflection_path = shader_reflection_path(asset_path);
    if (!reflection_path) {
        return {};
    }
    return parse_shader_reflection_bindings(
        Reader(reflection_path.value()).as_string()
    );
}

std::expected<std::unique_ptr<Shader>, std::error_code>
ShaderLoader::load(Reader& reader, const LoadContext& context) {
    auto manifest = parse_shader_manifest(
        reader.as_string_view(),
        context.asset_path().as_string()
    );
    auto stage = shader_stage_from_path(manifest.logical_path);
    if (!stage) {
        fei::error(
            "Unknown shader extension: {}",
            manifest.logical_path.string()
        );
        return nullptr;
    }

    std::string source = Reader(manifest.opengl_path).as_string();
    auto spirv = read_shader_binary(manifest.spirv_path);
    auto resources = parse_shader_reflection_bindings(
        Reader(manifest.reflection_path).as_string()
    );

    return std::make_unique<Shader>(Shader {
        .path = manifest.logical_path,
        .source = std::move(source),
        .spirv = std::move(spirv),
        .stage = stage.value(),
        .resources = std::move(resources),
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

Reader ShaderAssetSource::get_reader(const std::filesystem::path& path) const {
    if (!exists(path)) {
        fatal(
            "Generated shader output is missing for '{}'. Build the shader "
            "owner target, for example `xmake build -y fei-pbr`.",
            path.string()
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
