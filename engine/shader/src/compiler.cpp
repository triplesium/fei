#include "shader/compiler.hpp"

#include "artifact_cache.hpp"
#include "base/log.hpp"
#include "shader/shader.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fei {

namespace {

ShaderCompileError shader_compile_error(std::string message) {
    return ShaderCompileError {.message = std::move(message)};
}

bool shader_compile_target_enabled(
    ShaderCompileTarget compiler_target,
    ShaderCompileTarget requested_target
) {
    return compiler_target == ShaderCompileTarget::All ||
           compiler_target == requested_target;
}

std::filesystem::path
with_suffix(std::filesystem::path path, std::string_view suffix) {
    path += suffix;
    return path;
}

std::filesystem::path normalized_absolute_path(std::filesystem::path path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (!error) {
        path = std::move(absolute);
    }
    return path.lexically_normal();
}

std::string shader_stage_entry_name(ShaderStages stage) {
    switch (stage) {
        case ShaderStages::Vertex:
            return "vertex_main";
        case ShaderStages::Geometry:
            return "geometry_main";
        case ShaderStages::Fragment:
            return "fragment_main";
        case ShaderStages::Compute:
            return "compute_main";
        default:
            return {};
    }
}

std::filesystem::path strip_slang_suffix(std::filesystem::path path) {
    if (path.extension() == ".slang") {
        path.replace_extension();
    }
    return path;
}

std::string shader_def_define_value(const ShaderDefValue& value) {
    return std::visit(
        [](const auto& typed_value) -> std::string {
            using Value = std::decay_t<decltype(typed_value)>;
            if constexpr (std::is_same_v<Value, bool>) {
                return typed_value ? "1" : "0";
            } else {
                return std::to_string(typed_value);
            }
        },
        value
    );
}

RuntimeShaderCompilerConfig
normalize_runtime_shader_compiler_config(RuntimeShaderCompilerConfig config) {
    if (config.shader_sources.empty()) {
        if (!config.source_root.empty()) {
            config.shader_sources.add_root({}, config.source_root);
        } else {
            config.shader_sources = generated_shader_source_registry();
        }
    }

    if (config.source_root.empty()) {
        auto roots = config.shader_sources.roots();
        if (!roots.empty()) {
            config.source_root = roots.front();
        }
    }
    return config;
}

std::string
shader_source_roots_message(const RuntimeShaderCompilerConfig& config) {
    auto roots = config.shader_sources.roots();
    if (roots.empty()) {
        return "<none>";
    }

    std::string message;
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (i > 0) {
            message += ", ";
        }
        message += roots[i].string();
    }
    return message;
}

ShaderCompileError shader_source_not_found_error(
    const RuntimeShaderCompilerConfig& config,
    const std::filesystem::path& logical_path
) {
    auto stage_specific_slang = with_suffix(logical_path, ".slang");
    auto base_slang = logical_path;
    base_slang.replace_extension(".slang");

    return shader_compile_error(
        "Runtime Slang shader source not found for " + logical_path.string() +
        "; expected " + stage_specific_slang.string() + " or " +
        base_slang.string() + " under " + shader_source_roots_message(config)
    );
}

Result<ShaderCompileRequest, ShaderCompileError>
make_runtime_shader_compile_request(
    const RuntimeShaderCompilerConfig& config,
    std::shared_ptr<const ShaderSourceSnapshot> source_snapshot,
    std::filesystem::path logical_path,
    std::string source,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    logical_path = logical_path.lexically_normal();
    if (stage == ShaderStages::None) {
        return failure(shader_compile_error(
            "Unsupported shader stage for: " + logical_path.string()
        ));
    }

    auto source_path = source_snapshot ?
                           source_snapshot->resolve(logical_path) :
                           config.shader_sources.resolve(logical_path);
    std::filesystem::path source_root = config.source_root;
    std::filesystem::path resolved_source_path;
    std::filesystem::path relative_source = logical_path;
    if (!source_path) {
        if (source.empty()) {
            return failure(shader_source_not_found_error(config, logical_path));
        }
        resolved_source_path = (source_root / logical_path).lexically_normal();
    } else {
        auto resolved_source = std::move(source_path).value();
        source_root = std::move(resolved_source.root);
        relative_source = std::move(resolved_source.relative_path);
        resolved_source_path = std::move(resolved_source.source_path);
    }

    auto normalized_defs = normalized_shader_defs(std::move(defs));
    if (entry.empty()) {
        entry = "main";
        if (strip_slang_suffix(relative_source).generic_string() !=
            logical_path.generic_string()) {
            entry = shader_stage_entry_name(stage);
        }
    }

    return ShaderCompileRequest {
        .source_path = std::move(resolved_source_path),
        .source_root = std::move(source_root),
        .search_roots = config.shader_sources.roots(),
        .logical_path = std::move(logical_path),
        .source = std::move(source),
        .stage = stage,
        .entry = std::move(entry),
        .defs = std::move(normalized_defs),
        .source_snapshot = std::move(source_snapshot),
        .target = config.target,
    };
}

Result<std::string, ShaderCompileError>
read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return failure(shader_compile_error(
            "Failed to open shader source: " + path.string()
        ));
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

void insert_unique_dependency(
    std::vector<std::filesystem::path>& dependencies,
    const std::filesystem::path& path
) {
    auto normalized = normalized_absolute_path(path);
    if (std::find(dependencies.begin(), dependencies.end(), normalized) ==
        dependencies.end()) {
        dependencies.push_back(std::move(normalized));
    }
}

std::string blob_text(slang::IBlob* blob) {
    if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0) {
        return {};
    }
    const auto* data = static_cast<const char*>(blob->getBufferPointer());
    return std::string(data, data + blob->getBufferSize());
}

void use_write_only_storage_texture_access(std::string& wgsl) {
    constexpr std::string_view storage_prefix = "texture_storage_";
    constexpr std::string_view read_write = "read_write";
    std::size_t search_from = 0;
    while (true) {
        auto storage = wgsl.find(storage_prefix, search_from);
        if (storage == std::string::npos) {
            return;
        }
        auto declaration_end = wgsl.find(';', storage);
        auto access = wgsl.find(read_write, storage);
        if (declaration_end == std::string::npos ||
            access == std::string::npos || access > declaration_end) {
            search_from = storage + storage_prefix.size();
            continue;
        }
        auto variable = wgsl.rfind("var ", storage);
        if (variable == std::string::npos) {
            search_from = declaration_end + 1;
            continue;
        }
        auto name_begin = variable + 4;
        auto name_end = wgsl.find_first_of(" :", name_begin);
        if (name_end == std::string::npos || name_end > storage) {
            search_from = declaration_end + 1;
            continue;
        }
        auto name = wgsl.substr(name_begin, name_end - name_begin);
        bool write_only = true;
        auto use = wgsl.find(name, declaration_end + 1);
        while (use != std::string::npos) {
            auto statement_begin = wgsl.find_last_of(";{}\n", use);
            auto store = wgsl.rfind("textureStore", use);
            if (store == std::string::npos ||
                (statement_begin != std::string::npos &&
                 store < statement_begin)) {
                write_only = false;
                break;
            }
            use = wgsl.find(name, use + name.size());
        }
        if (write_only) {
            wgsl.replace(access, read_write.size(), "write");
        }
        search_from = declaration_end + 1;
    }
}

ShaderCompileError
slang_compile_error(std::string message, slang::IBlob* diagnostics = nullptr) {
    return ShaderCompileError {
        .message = std::move(message),
        .diagnostics = blob_text(diagnostics),
    };
}

SlangStage slang_stage_from_shader_stage(ShaderStages stage) {
    switch (stage) {
        case ShaderStages::Vertex:
            return SLANG_STAGE_VERTEX;
        case ShaderStages::Geometry:
            return SLANG_STAGE_GEOMETRY;
        case ShaderStages::Fragment:
            return SLANG_STAGE_FRAGMENT;
        case ShaderStages::Compute:
            return SLANG_STAGE_COMPUTE;
        default:
            return SLANG_STAGE_NONE;
    }
}

Result<std::vector<std::byte>, ShaderCompileError>
blob_bytes(slang::IBlob* blob) {
    if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0) {
        return failure(
            shader_compile_error("Slang returned an empty code blob")
        );
    }

    std::vector<std::byte> bytes(blob->getBufferSize());
    std::memcpy(bytes.data(), blob->getBufferPointer(), bytes.size());
    return bytes;
}

bool is_slang_descriptor_resource_category(slang::ParameterCategory category) {
    switch (category) {
        case slang::ParameterCategory::ConstantBuffer:
        case slang::ParameterCategory::ShaderResource:
        case slang::ParameterCategory::UnorderedAccess:
        case slang::ParameterCategory::SamplerState:
        case slang::ParameterCategory::DescriptorTableSlot:
        case slang::ParameterCategory::GenericResource:
            return true;
        default:
            return false;
    }
}

bool is_slang_descriptor_resource(slang::VariableLayoutReflection& parameter) {
    if (is_slang_descriptor_resource_category(parameter.getCategory())) {
        return true;
    }

    const auto category_count = parameter.getCategoryCount();
    for (unsigned i = 0; i < category_count; ++i) {
        if (is_slang_descriptor_resource_category(
                parameter.getCategoryByIndex(i)
            )) {
            return true;
        }
    }
    return false;
}

bool is_slang_unknown_binding(unsigned value) {
    return value == static_cast<unsigned>(SLANG_UNKNOWN_SIZE);
}

bool is_slang_unknown_size(std::size_t value) {
    return value == static_cast<std::size_t>(SLANG_UNKNOWN_SIZE) ||
           value == static_cast<std::size_t>(SLANG_UNBOUNDED_SIZE);
}

bool is_slang_parameter_block(slang::VariableLayoutReflection& parameter) {
    auto* type = parameter.getType();
    if (type != nullptr &&
        type->getKind() == slang::TypeReflection::Kind::ParameterBlock) {
        return true;
    }

    auto* type_layout = parameter.getTypeLayout();
    return type_layout != nullptr &&
           type_layout->getKind() ==
               slang::TypeReflection::Kind::ParameterBlock;
}

bool has_slang_ordinary_data(slang::TypeLayoutReflection& type_layout) {
    const auto size = type_layout.getSize(slang::ParameterCategory::Uniform);
    return !is_slang_unknown_size(size) && size > 0;
}

std::string slang_variable_name(slang::VariableLayoutReflection& variable) {
    const char* name = variable.getName();
    return name == nullptr ? std::string {} : std::string {name};
}

std::optional<ResourceKind>
slang_resource_kind(slang::VariableLayoutReflection& parameter) {
    auto* type_layout = parameter.getTypeLayout();
    if (type_layout == nullptr) {
        return std::nullopt;
    }
    type_layout = type_layout->unwrapArray();

    const auto kind = type_layout->getKind();
    if (kind == slang::TypeReflection::Kind::ConstantBuffer ||
        parameter.getCategory() == slang::ParameterCategory::ConstantBuffer) {
        return ResourceKind::UniformBuffer;
    }
    if (kind == slang::TypeReflection::Kind::SamplerState ||
        parameter.getCategory() == slang::ParameterCategory::SamplerState) {
        return ResourceKind::Sampler;
    }

    const auto shape =
        type_layout->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK;
    const bool buffer =
        shape == SLANG_STRUCTURED_BUFFER ||
        shape == SLANG_BYTE_ADDRESS_BUFFER ||
        kind == slang::TypeReflection::Kind::ShaderStorageBuffer;
    const auto access = type_layout->getResourceAccess();
    const bool writable = access != SLANG_RESOURCE_ACCESS_NONE &&
                          access != SLANG_RESOURCE_ACCESS_READ;
    if (buffer) {
        return writable ? ResourceKind::StorageBufferReadWrite :
                          ResourceKind::StorageBufferReadOnly;
    }
    if (kind == slang::TypeReflection::Kind::Resource ||
        shape != SLANG_RESOURCE_NONE) {
        return writable ? ResourceKind::TextureReadWrite :
                          ResourceKind::TextureReadOnly;
    }
    return std::nullopt;
}

void append_slang_resource_binding(
    std::vector<ShaderResourceBinding>& bindings,
    std::string name,
    ResourceKind kind,
    uint32_t set,
    uint32_t binding,
    uint32_t array_size = 1
) {
    if (name.empty()) {
        return;
    }

    auto it = std::find_if(
        bindings.begin(),
        bindings.end(),
        [&](const ShaderResourceBinding& resource) {
            return resource.set == set && resource.binding == binding &&
                   resource.name == name;
        }
    );
    if (it != bindings.end()) {
        return;
    }

    auto backend_name = name;
    bindings.push_back(
        ShaderResourceBinding {
            .name = std::move(name),
            .backend_name = backend_name,
            .backend_names = {std::move(backend_name)},
            .kind = kind,
            .set = set,
            .binding = binding,
            .array_size = array_size,
        }
    );
}

uint32_t
slang_descriptor_binding_count(slang::TypeLayoutReflection& type_layout) {
    if (!type_layout.isArray()) {
        return 1;
    }

    const auto count = type_layout.getTotalArrayElementCount();
    if (is_slang_unknown_size(count) || count == 0) {
        return 1;
    }
    return static_cast<uint32_t>(count);
}

bool slang_parameter_block_set(
    slang::VariableLayoutReflection& parameter,
    uint32_t& set
) {
    const auto register_space =
        parameter.getOffset(slang::ParameterCategory::SubElementRegisterSpace);
    if (!is_slang_unknown_size(register_space)) {
        set = static_cast<uint32_t>(register_space);
        return true;
    }

    const auto binding_index = parameter.getBindingIndex();
    if (!is_slang_unknown_binding(binding_index)) {
        set = static_cast<uint32_t>(binding_index);
        return true;
    }

    const auto category_space = parameter.getBindingSpace(
        slang::ParameterCategory::SubElementRegisterSpace
    );
    if (!is_slang_unknown_size(category_space)) {
        set = static_cast<uint32_t>(category_space);
        return true;
    }

    const auto binding_space = parameter.getBindingSpace();
    if (!is_slang_unknown_binding(binding_space)) {
        set = static_cast<uint32_t>(binding_space);
        return true;
    }

    return false;
}

void append_slang_parameter_block_resource_bindings(
    std::vector<ShaderResourceBinding>& bindings,
    slang::VariableLayoutReflection& parameter
) {
    uint32_t set = 0;
    if (!slang_parameter_block_set(parameter, set)) {
        return;
    }

    auto* block_layout = parameter.getTypeLayout();
    if (block_layout == nullptr) {
        return;
    }
    auto* element_layout = block_layout->getElementTypeLayout();
    if (element_layout == nullptr) {
        return;
    }

    uint32_t binding = 0;
    if (has_slang_ordinary_data(*element_layout)) {
        append_slang_resource_binding(
            bindings,
            slang_variable_name(parameter),
            ResourceKind::UniformBuffer,
            set,
            binding
        );
        ++binding;
    }

    const auto field_count = element_layout->getFieldCount();
    for (unsigned i = 0; i < field_count; ++i) {
        auto* field = element_layout->getFieldByIndex(i);
        if (field == nullptr || !is_slang_descriptor_resource(*field)) {
            continue;
        }

        auto* field_type_layout = field->getTypeLayout();
        const auto binding_count =
            field_type_layout == nullptr ?
                1 :
                slang_descriptor_binding_count(*field_type_layout);
        if (auto kind = slang_resource_kind(*field)) {
            append_slang_resource_binding(
                bindings,
                slang_variable_name(*field),
                *kind,
                set,
                binding,
                binding_count
            );
        }
        binding += binding_count;
    }
}

void append_slang_global_parameter_block_resource_bindings(
    std::vector<ShaderResourceBinding>& bindings,
    slang::ProgramLayout& layout
) {
    auto* global_params = layout.getGlobalParamsTypeLayout();
    if (global_params == nullptr) {
        return;
    }

    const auto field_count = global_params->getFieldCount();
    for (unsigned i = 0; i < field_count; ++i) {
        auto* field = global_params->getFieldByIndex(i);
        if (field == nullptr || !is_slang_parameter_block(*field)) {
            continue;
        }
        append_slang_parameter_block_resource_bindings(bindings, *field);
    }
}

Result<std::vector<ShaderResourceBinding>, ShaderCompileError>
slang_resource_bindings(slang::IComponentType& linked_program) {
    Slang::ComPtr<slang::IBlob> layout_diagnostics;
    auto* layout = linked_program.getLayout(0, layout_diagnostics.writeRef());
    if (layout == nullptr) {
        return failure(slang_compile_error(
            "Slang failed to generate reflection layout",
            layout_diagnostics
        ));
    }

    std::vector<ShaderResourceBinding> bindings;
    const auto parameter_count = layout->getParameterCount();
    bindings.reserve(parameter_count);
    for (unsigned i = 0; i < parameter_count; ++i) {
        auto* parameter = layout->getParameterByIndex(i);
        if (parameter == nullptr) {
            continue;
        }

        if (is_slang_parameter_block(*parameter)) {
            append_slang_parameter_block_resource_bindings(
                bindings,
                *parameter
            );
            continue;
        }

        if (!is_slang_descriptor_resource(*parameter)) {
            continue;
        }

        const auto binding = parameter->getBindingIndex();
        const auto space = parameter->getBindingSpace();
        if (is_slang_unknown_binding(binding) ||
            is_slang_unknown_binding(space)) {
            continue;
        }

        auto kind = slang_resource_kind(*parameter);
        if (!kind) {
            continue;
        }
        auto* type_layout = parameter->getTypeLayout();
        append_slang_resource_binding(
            bindings,
            slang_variable_name(*parameter),
            *kind,
            static_cast<uint32_t>(space),
            static_cast<uint32_t>(binding),
            type_layout == nullptr ?
                1 :
                slang_descriptor_binding_count(*type_layout)
        );
    }

    append_slang_global_parameter_block_resource_bindings(bindings, *layout);

    return bindings;
}

struct SlangMacroStorage {
    std::vector<std::string> names;
    std::vector<std::string> values;
    std::vector<slang::PreprocessorMacroDesc> macros;
};

SlangMacroStorage make_slang_macro_storage(ShaderDefs defs) {
    defs = normalized_shader_defs(std::move(defs));

    SlangMacroStorage storage;
    storage.names.reserve(defs.size());
    storage.values.reserve(defs.size());
    storage.macros.reserve(defs.size());
    for (const auto& def : defs) {
        storage.names.push_back(def.name);
        storage.values.push_back(shader_def_define_value(def.value));
        storage.macros.push_back(
            slang::PreprocessorMacroDesc {
                .name = storage.names.back().c_str(),
                .value = storage.values.back().c_str(),
            }
        );
    }
    return storage;
}

Result<Slang::ComPtr<slang::IEntryPoint>, ShaderCompileError>
find_slang_entry_point(
    slang::IModule& module,
    const ShaderCompileRequest& request
) {
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    auto result = module.findEntryPointByName(
        request.entry.c_str(),
        entry_point.writeRef()
    );
    if (SLANG_SUCCEEDED(result) && entry_point) {
        return entry_point;
    }

    auto stage = slang_stage_from_shader_stage(request.stage);
    if (stage == SLANG_STAGE_NONE) {
        return failure(shader_compile_error("Unsupported shader stage"));
    }

    Slang::ComPtr<slang::IBlob> diagnostics;
    result = module.findAndCheckEntryPoint(
        request.entry.c_str(),
        stage,
        entry_point.writeRef(),
        diagnostics.writeRef()
    );
    if (SLANG_FAILED(result) || !entry_point) {
        return failure(slang_compile_error(
            "Slang failed to find shader entry point: " + request.entry,
            diagnostics
        ));
    }
    return entry_point;
}

struct SlangCompileOutput {
    std::vector<std::byte> spirv;
    std::string wgsl;
    std::vector<ShaderResourceBinding> resources;
    std::vector<std::filesystem::path> dependencies;
    std::vector<ShaderDependencySnapshot> dependency_snapshots;
};

class TrackingSlangFileSystem final : public ISlangFileSystemExt {
  private:
    std::shared_ptr<const ShaderSourceSnapshot> m_source_snapshot;

  public:
    explicit TrackingSlangFileSystem(
        std::shared_ptr<const ShaderSourceSnapshot> source_snapshot
    ) : m_source_snapshot(std::move(source_snapshot)) {}

    SLANG_NO_THROW SlangResult SLANG_MCALL
    queryInterface(SlangUUID const& uuid, void** out_object) override {
        if (out_object == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        if (auto* intf = get_interface(uuid)) {
            addRef();
            *out_object = intf;
            return SLANG_OK;
        }
        *out_object = nullptr;
        return SLANG_E_NO_INTERFACE;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override {
        return ++m_ref_count;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        const auto ref_count = --m_ref_count;
        if (ref_count == 0) {
            delete this;
        }
        return ref_count;
    }

    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override {
        return get_interface(guid);
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL
    loadFile(char const* path, ISlangBlob** out_blob) override {
        if (path == nullptr || out_blob == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *out_blob = nullptr;

        std::string contents;
        if (m_source_snapshot) {
            auto source = m_source_snapshot->source(path);
            if (!source) {
                return SLANG_E_NOT_FOUND;
            }
            contents = *source;
        } else {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return SLANG_E_NOT_FOUND;
            }
            contents.assign(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            );
        }
        *out_blob = slang_createBlob(contents.data(), contents.size());
        if (*out_blob == nullptr) {
            return SLANG_E_OUT_OF_MEMORY;
        }

        auto dependency_path = normalized_absolute_path(path);
        insert_unique_dependency(m_dependencies, dependency_path);
        m_dependency_snapshots.push_back(
            ShaderDependencySnapshot {
                .path = std::move(dependency_path),
                .source = std::move(contents),
            }
        );
        return SLANG_OK;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL getFileUniqueIdentity(
        const char* path,
        ISlangBlob** out_unique_identity
    ) override {
        if (path == nullptr || out_unique_identity == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *out_unique_identity = nullptr;
        return make_path_blob(
            canonical_source_path(std::filesystem::path(path)),
            out_unique_identity
        );
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL calcCombinedPath(
        SlangPathType from_path_type,
        const char* from_path,
        const char* path,
        ISlangBlob** path_out
    ) override {
        if (from_path == nullptr || path == nullptr || path_out == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *path_out = nullptr;

        auto dependency_path = std::filesystem::path(path);
        if (!dependency_path.is_absolute()) {
            auto base = std::filesystem::path(from_path);
            if (from_path_type == SLANG_PATH_TYPE_FILE) {
                base = base.parent_path();
            }
            dependency_path = base / dependency_path;
        }
        return make_path_blob(dependency_path.lexically_normal(), path_out);
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL
    getPathType(const char* path, SlangPathType* path_type_out) override {
        if (path == nullptr || path_type_out == nullptr) {
            return SLANG_E_INVALID_ARG;
        }

        if (m_source_snapshot) {
            if (m_source_snapshot->is_file(path)) {
                *path_type_out = SLANG_PATH_TYPE_FILE;
                return SLANG_OK;
            }
            if (m_source_snapshot->is_directory(path)) {
                *path_type_out = SLANG_PATH_TYPE_DIRECTORY;
                return SLANG_OK;
            }
        } else {
            std::error_code error;
            const auto status = std::filesystem::status(path, error);
            if (!error && std::filesystem::is_directory(status)) {
                *path_type_out = SLANG_PATH_TYPE_DIRECTORY;
                return SLANG_OK;
            }
            if (!error && std::filesystem::is_regular_file(status)) {
                *path_type_out = SLANG_PATH_TYPE_FILE;
                return SLANG_OK;
            }
        }
        return SLANG_E_NOT_FOUND;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL
    getPath(PathKind kind, const char* path, ISlangBlob** out_path) override {
        if (path == nullptr || out_path == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *out_path = nullptr;

        auto result = std::filesystem::path(path);
        switch (kind) {
            case PathKind::Canonical:
            case PathKind::OperatingSystem:
                result = canonical_source_path(result);
                break;
            case PathKind::Simplified:
            case PathKind::Display:
                result = result.lexically_normal();
                break;
            case PathKind::CountOf:
                return SLANG_E_INVALID_ARG;
        }
        return make_path_blob(result, out_path);
    }

    SLANG_NO_THROW void SLANG_MCALL clearCache() override {}

    SLANG_NO_THROW SlangResult SLANG_MCALL enumeratePathContents(
        const char* /*path*/,
        FileSystemContentsCallBack /*callback*/,
        void* /*userData*/
    ) override {
        return SLANG_E_NOT_IMPLEMENTED;
    }

    SLANG_NO_THROW OSPathKind SLANG_MCALL getOSPathKind() override {
        return OSPathKind::Direct;
    }

    [[nodiscard]] const std::vector<std::filesystem::path>&
    dependencies() const {
        return m_dependencies;
    }

    [[nodiscard]] const std::vector<ShaderDependencySnapshot>&
    dependency_snapshots() const {
        return m_dependency_snapshots;
    }

  private:
    ISlangUnknown* get_interface(const SlangUUID& uuid) {
        if (uuid == ISlangUnknown::getTypeGuid() ||
            uuid == ISlangCastable::getTypeGuid() ||
            uuid == ISlangFileSystem::getTypeGuid() ||
            uuid == ISlangFileSystemExt::getTypeGuid()) {
            return static_cast<ISlangFileSystemExt*>(this);
        }
        return nullptr;
    }

    static std::filesystem::path
    canonical_path(const std::filesystem::path& path) {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(path, error);
        if (!error) {
            return canonical.lexically_normal();
        }
        return normalized_absolute_path(path);
    }

    [[nodiscard]] std::filesystem::path
    canonical_source_path(const std::filesystem::path& path) const {
        if (m_source_snapshot) {
            return normalized_absolute_path(path);
        }
        return canonical_path(path);
    }

    static SlangResult
    make_path_blob(const std::filesystem::path& path, ISlangBlob** out_blob) {
        auto string = path.lexically_normal().string();
        *out_blob = slang_createBlob(string.c_str(), string.size() + 1);
        return *out_blob == nullptr ? SLANG_E_OUT_OF_MEMORY : SLANG_OK;
    }

    std::atomic<uint32_t> m_ref_count {0};
    std::vector<std::filesystem::path> m_dependencies;
    std::vector<ShaderDependencySnapshot> m_dependency_snapshots;
};

Slang::ComPtr<TrackingSlangFileSystem> make_tracking_slang_file_system(
    std::shared_ptr<const ShaderSourceSnapshot> source_snapshot
) {
    auto* file_system = new TrackingSlangFileSystem(std::move(source_snapshot));
    file_system->addRef();
    return Slang::ComPtr<TrackingSlangFileSystem>(
        Slang::INIT_ATTACH,
        file_system
    );
}

Result<SlangCompileOutput, ShaderCompileError>
compile_slang(const ShaderCompileRequest& request) {
    std::string source = request.source;
    if (source.empty()) {
        if (request.source_snapshot) {
            auto snapshot_source =
                request.source_snapshot->source(request.source_path);
            if (!snapshot_source) {
                return failure(shader_compile_error(
                    "Shader source is absent from the Render World snapshot: " +
                    request.source_path.string()
                ));
            }
            source = *snapshot_source;
        } else {
            auto file_source = read_text_file(request.source_path);
            if (!file_source) {
                return failure(std::move(file_source).error());
            }
            source = std::move(file_source).value();
        }
    }

    Slang::ComPtr<slang::IGlobalSession> global_session;
    auto result = slang::createGlobalSession(global_session.writeRef());
    if (SLANG_FAILED(result) || !global_session) {
        return failure(
            shader_compile_error("Failed to create Slang global session")
        );
    }

    const bool primary_is_wgsl = request.target == ShaderCompileTarget::WebGpu;
    slang::TargetDesc target_desc {};
    target_desc.format = primary_is_wgsl ? SLANG_WGSL : SLANG_SPIRV;
    if (!primary_is_wgsl) {
        target_desc.profile = global_session->findProfile("glsl_450");
    }

    auto primary_defs = request.defs;
    if (primary_is_wgsl) {
        std::erase_if(primary_defs, [](const ShaderDefVal& def) {
            return def.name == "FEI_SHADER_TARGET_WGSL";
        });
        primary_defs.push_back(
            ShaderDefVal::bool_def("FEI_SHADER_TARGET_WGSL")
        );
    }
    auto macros = make_slang_macro_storage(std::move(primary_defs));
    auto search_roots = request.search_roots;
    if (search_roots.empty() && !request.source_root.empty()) {
        search_roots.push_back(request.source_root);
    }

    std::vector<std::string> search_path_storage;
    std::vector<const char*> search_paths;
    search_path_storage.reserve(search_roots.size());
    search_paths.reserve(search_roots.size());
    for (const auto& root : search_roots) {
        search_path_storage.push_back(root.string());
        search_paths.push_back(search_path_storage.back().c_str());
    }
    auto file_system = make_tracking_slang_file_system(request.source_snapshot);

    slang::SessionDesc session_desc {};
    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;
    session_desc.searchPaths =
        search_paths.empty() ? nullptr : search_paths.data();
    session_desc.searchPathCount = static_cast<SlangInt>(search_paths.size());
    session_desc.preprocessorMacros = macros.macros.data();
    session_desc.preprocessorMacroCount =
        static_cast<SlangInt>(macros.macros.size());
    session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    session_desc.fileSystem = file_system.get();

    Slang::ComPtr<slang::ISession> session;
    result = global_session->createSession(session_desc, session.writeRef());
    if (SLANG_FAILED(result) || !session) {
        return failure(shader_compile_error("Failed to create Slang session"));
    }

    auto module_name = request.source_path.stem().string();
    if (module_name.empty()) {
        module_name = "shader";
    }
    auto source_path = request.source_path.string();
    Slang::ComPtr<slang::IBlob> load_diagnostics;
    slang::IModule* module = session->loadModuleFromSourceString(
        module_name.c_str(),
        source_path.c_str(),
        source.c_str(),
        load_diagnostics.writeRef()
    );
    if (!module) {
        return failure(slang_compile_error(
            "Slang failed to load shader module: " + source_path,
            load_diagnostics
        ));
    }

    auto entry_point = find_slang_entry_point(*module, request);
    if (!entry_point) {
        return failure(std::move(entry_point).error());
    }

    slang::IComponentType* components[] = {module, entry_point->get()};
    Slang::ComPtr<slang::IComponentType> program;
    Slang::ComPtr<slang::IBlob> compose_diagnostics;
    result = session->createCompositeComponentType(
        components,
        2,
        program.writeRef(),
        compose_diagnostics.writeRef()
    );
    if (SLANG_FAILED(result) || !program) {
        return failure(slang_compile_error(
            "Slang failed to create shader program",
            compose_diagnostics
        ));
    }

    Slang::ComPtr<slang::IComponentType> linked_program;
    Slang::ComPtr<slang::IBlob> link_diagnostics;
    result =
        program->link(linked_program.writeRef(), link_diagnostics.writeRef());
    if (SLANG_FAILED(result) || !linked_program) {
        return failure(slang_compile_error(
            "Slang failed to link shader program",
            link_diagnostics
        ));
    }

    Slang::ComPtr<slang::IBlob> code;
    Slang::ComPtr<slang::IBlob> code_diagnostics;
    result = linked_program->getEntryPointCode(
        0,
        0,
        code.writeRef(),
        code_diagnostics.writeRef()
    );
    if (SLANG_FAILED(result) || !code) {
        return failure(slang_compile_error(
            primary_is_wgsl ? "Slang failed to generate WGSL" :
                              "Slang failed to generate SPIR-V",
            code_diagnostics
        ));
    }

    std::vector<std::byte> spirv;
    std::string wgsl;
    if (primary_is_wgsl) {
        wgsl = blob_text(code);
        if (!wgsl.empty() && wgsl.back() == '\0') {
            wgsl.pop_back();
        }
        use_write_only_storage_texture_access(wgsl);
    } else {
        auto bytes = blob_bytes(code);
        if (!bytes) {
            return failure(std::move(bytes).error());
        }
        spirv = std::move(bytes).value();
    }

    auto resources = slang_resource_bindings(*linked_program);
    if (!resources) {
        return failure(std::move(resources).error());
    }

    auto compile_wgsl = [&]() -> std::string {
        slang::TargetDesc wgsl_target_desc {};
        wgsl_target_desc.format = SLANG_WGSL;

        auto wgsl_defs = request.defs;
        std::erase_if(wgsl_defs, [](const ShaderDefVal& def) {
            return def.name == "FEI_SHADER_TARGET_WGSL";
        });
        wgsl_defs.push_back(ShaderDefVal::bool_def("FEI_SHADER_TARGET_WGSL"));
        auto wgsl_macros = make_slang_macro_storage(std::move(wgsl_defs));

        auto wgsl_file_system =
            make_tracking_slang_file_system(request.source_snapshot);
        auto wgsl_session_desc = session_desc;
        wgsl_session_desc.targets = &wgsl_target_desc;
        wgsl_session_desc.fileSystem = wgsl_file_system.get();
        wgsl_session_desc.preprocessorMacros = wgsl_macros.macros.data();
        wgsl_session_desc.preprocessorMacroCount =
            static_cast<SlangInt>(wgsl_macros.macros.size());

        Slang::ComPtr<slang::ISession> wgsl_session;
        auto wgsl_result = global_session->createSession(
            wgsl_session_desc,
            wgsl_session.writeRef()
        );
        if (SLANG_FAILED(wgsl_result) || !wgsl_session) {
            trace(
                "Slang skipped WGSL session creation for '{}'",
                request.logical_path.string()
            );
            return {};
        }

        Slang::ComPtr<slang::IBlob> wgsl_diagnostics;
        auto* wgsl_module = wgsl_session->loadModuleFromSourceString(
            module_name.c_str(),
            source_path.c_str(),
            source.c_str(),
            wgsl_diagnostics.writeRef()
        );
        if (wgsl_module == nullptr) {
            trace(
                "Slang skipped WGSL module generation for '{}': {}",
                request.logical_path.string(),
                blob_text(wgsl_diagnostics)
            );
            return {};
        }

        auto wgsl_entry_point = find_slang_entry_point(*wgsl_module, request);
        if (!wgsl_entry_point) {
            trace(
                "Slang skipped WGSL entry point generation for '{}': {} {}",
                request.logical_path.string(),
                wgsl_entry_point.error().message,
                wgsl_entry_point.error().diagnostics
            );
            return {};
        }

        slang::IComponentType* wgsl_components[] = {
            wgsl_module,
            wgsl_entry_point->get(),
        };
        Slang::ComPtr<slang::IComponentType> wgsl_program;
        wgsl_diagnostics.setNull();
        wgsl_result = wgsl_session->createCompositeComponentType(
            wgsl_components,
            2,
            wgsl_program.writeRef(),
            wgsl_diagnostics.writeRef()
        );
        if (SLANG_FAILED(wgsl_result) || !wgsl_program) {
            trace(
                "Slang skipped WGSL program generation for '{}': {}",
                request.logical_path.string(),
                blob_text(wgsl_diagnostics)
            );
            return {};
        }

        Slang::ComPtr<slang::IComponentType> wgsl_linked_program;
        wgsl_diagnostics.setNull();
        wgsl_result = wgsl_program->link(
            wgsl_linked_program.writeRef(),
            wgsl_diagnostics.writeRef()
        );
        if (SLANG_FAILED(wgsl_result) || !wgsl_linked_program) {
            trace(
                "Slang skipped WGSL link for '{}': {}",
                request.logical_path.string(),
                blob_text(wgsl_diagnostics)
            );
            return {};
        }

        Slang::ComPtr<slang::IBlob> wgsl_code;
        wgsl_diagnostics.setNull();
        wgsl_result = wgsl_linked_program->getEntryPointCode(
            0,
            0,
            wgsl_code.writeRef(),
            wgsl_diagnostics.writeRef()
        );
        if (SLANG_FAILED(wgsl_result) || !wgsl_code) {
            trace(
                "Slang skipped WGSL code generation for '{}': {}",
                request.logical_path.string(),
                blob_text(wgsl_diagnostics)
            );
            return {};
        }

        auto wgsl = blob_text(wgsl_code);
        if (!wgsl.empty() && wgsl.back() == '\0') {
            wgsl.pop_back();
        }
        use_write_only_storage_texture_access(wgsl);
        return wgsl;
    };

    if (request.target == ShaderCompileTarget::All &&
        request.stage != ShaderStages::Geometry) {
        wgsl = compile_wgsl();
    }

    std::vector<std::filesystem::path> dependencies;
    insert_unique_dependency(dependencies, request.source_path);
    for (const auto& dependency : file_system->dependencies()) {
        insert_unique_dependency(dependencies, dependency);
    }
    auto dependency_snapshots = file_system->dependency_snapshots();
    dependency_snapshots.push_back(
        ShaderDependencySnapshot {
            .path = normalized_absolute_path(request.source_path),
            .source = source,
        }
    );

    return SlangCompileOutput {
        .spirv = std::move(spirv),
        .wgsl = std::move(wgsl),
        .resources = std::move(resources).value(),
        .dependencies = std::move(dependencies),
        .dependency_snapshots = std::move(dependency_snapshots),
    };
}

} // namespace

BoxedShaderCompiler::BoxedShaderCompiler(
    std::unique_ptr<ShaderCompiler> compiler
) : m_compiler(std::move(compiler)) {
    if (!m_compiler) {
        throw std::invalid_argument("BoxedShaderCompiler requires a compiler");
    }
}

std::string BoxedShaderCompiler::cache_identity() const {
    return m_compiler->cache_identity();
}

Result<ShaderCompileOutput, ShaderCompileError>
BoxedShaderCompiler::compile(ShaderCompileRequest request) {
    return m_compiler->compile(std::move(request));
}

ShaderVariantCompiler::ShaderVariantCompiler(
    ShaderCompiler& compiler,
    RuntimeShaderCompilerConfig config
) :
    m_compiler(&compiler),
    m_config(normalize_runtime_shader_compiler_config(std::move(config))) {
    auto compiler_identity = compiler.cache_identity();
    if (!m_config.cache_root.empty() && !compiler_identity.empty()) {
        m_artifact_cache = std::make_shared<ShaderArtifactCache>(
            m_config.cache_root,
            std::move(compiler_identity)
        );
    }
}

Result<ShaderVariantCompileOutput, ShaderCompileError>
ShaderVariantCompiler::compile_with_dependencies(
    std::filesystem::path logical_path,
    ShaderDefs defs
) {
    auto stage = shader_stage_from_path(logical_path);
    if (!stage) {
        return failure(shader_compile_error(
            "Unsupported shader logical path: " + logical_path.string()
        ));
    }
    return compile_with_dependencies(
        std::move(logical_path),
        stage.value(),
        {},
        std::move(defs)
    );
}

Result<ShaderVariantCompileOutput, ShaderCompileError>
ShaderVariantCompiler::compile_with_dependencies(
    std::filesystem::path logical_path,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    return compile_with_dependencies(
        std::move(logical_path),
        {},
        stage,
        std::move(entry),
        std::move(defs)
    );
}

Result<ShaderVariantCompileOutput, ShaderCompileError>
ShaderVariantCompiler::compile_with_dependencies(
    std::filesystem::path logical_path,
    std::string source,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    if (m_compiler == nullptr) {
        return failure(shader_compile_error(
            "ShaderVariantCompiler requires a ShaderCompiler"
        ));
    }

    auto request = make_runtime_shader_compile_request(
        m_config,
        m_source_snapshot,
        std::move(logical_path),
        std::move(source),
        stage,
        std::move(entry),
        std::move(defs)
    );
    if (!request) {
        return failure(std::move(request).error());
    }

    auto compile_request = std::move(request).value();
    if (m_artifact_cache && compile_request.source.empty()) {
        if (compile_request.source_snapshot) {
            auto source = compile_request.source_snapshot->source(
                compile_request.source_path
            );
            if (!source) {
                return failure(shader_compile_error(
                    "Shader source is absent from the Render World snapshot: " +
                    compile_request.source_path.string()
                ));
            }
            compile_request.source = *source;
        } else {
            auto source = read_text_file(compile_request.source_path);
            if (!source) {
                return failure(std::move(source).error());
            }
            compile_request.source = std::move(source).value();
        }
    }
    std::optional<ShaderArtifactCache::Key> artifact_cache_key;
    if (m_artifact_cache) {
        artifact_cache_key = m_artifact_cache->key(compile_request);
        if (artifact_cache_key) {
            auto cached =
                m_artifact_cache->load(*artifact_cache_key, compile_request);
            if (cached) {
                trace(
                    "Shader cache hit for '{}' ({})",
                    compile_request.logical_path.string(),
                    compile_request.entry
                );
                return std::move(*cached);
            }
        }
    }

    auto output = m_compiler->compile(compile_request);
    if (!output) {
        return failure(std::move(output).error());
    }

    if (artifact_cache_key) {
        m_artifact_cache->store(*artifact_cache_key, compile_request, *output);
    }

    return ShaderVariantCompileOutput {
        .description = std::move(output->description),
        .dependencies = std::move(output->dependencies),
    };
}

Result<ShaderDescription, ShaderCompileError> ShaderVariantCompiler::compile(
    std::filesystem::path logical_path,
    ShaderDefs defs
) {
    auto output =
        compile_with_dependencies(std::move(logical_path), std::move(defs));
    if (!output) {
        return failure(std::move(output).error());
    }
    auto value = std::move(output).value();
    return std::move(value.description);
}

Result<ShaderDescription, ShaderCompileError> ShaderVariantCompiler::compile(
    std::filesystem::path logical_path,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    auto output = compile_with_dependencies(
        std::move(logical_path),
        stage,
        std::move(entry),
        std::move(defs)
    );
    if (!output) {
        return failure(std::move(output).error());
    }
    auto value = std::move(output).value();
    return std::move(value.description);
}

SlangLibraryShaderCompiler::SlangLibraryShaderCompiler(
    ShaderCompileTarget target,
    ShaderArtifactGenerator* artifact_generator
) : m_target(target), m_artifact_generator(artifact_generator) {}

std::string SlangLibraryShaderCompiler::cache_identity() const {
    auto* build_tag = spGetBuildTagString();
    if (build_tag == nullptr) {
        return {};
    }
    std::string identity = std::string(build_tag) + "|fei-shader-compiler-v5";
    if (m_artifact_generator != nullptr) {
        identity += '|' + m_artifact_generator->cache_identity();
    }
    return identity;
}

Result<ShaderCompileOutput, ShaderCompileError>
SlangLibraryShaderCompiler::compile(ShaderCompileRequest request) {
    request.defs = normalized_shader_defs(std::move(request.defs));
    if (!shader_compile_target_enabled(m_target, request.target)) {
        return failure(shader_compile_error(
            "Requested shader target is disabled in this build"
        ));
    }
    if (request.target == ShaderCompileTarget::WebGpu &&
        request.stage == ShaderStages::Geometry) {
        return failure(
            shader_compile_error("WebGPU does not support geometry shaders")
        );
    }

    auto slang = compile_slang(request);
    if (!slang) {
        return failure(std::move(slang).error());
    }

    std::string opengl_source;
    auto resources = std::move(slang->resources);
    if (request.target == ShaderCompileTarget::All ||
        request.target == ShaderCompileTarget::OpenGL) {
        if (m_artifact_generator == nullptr) {
            return failure(shader_compile_error(
                "OpenGL shader generation is unavailable for this compiler"
            ));
        }
        std::vector<ShaderArtifactLogicalResourceName> logical_resource_names;
        logical_resource_names.reserve(resources.size());
        for (const auto& resource : resources) {
            logical_resource_names.push_back(
                ShaderArtifactLogicalResourceName {
                    .name = resource.name,
                    .set = resource.set,
                    .binding = resource.binding,
                }
            );
        }
        auto artifacts = m_artifact_generator->generate(
            ShaderArtifactGenerationInput {
                .spirv = slang->spirv,
                .logical_resource_names = std::move(logical_resource_names),
            }
        );
        if (!artifacts) {
            return failure(std::move(artifacts).error());
        }
        opengl_source = std::move(artifacts->source);
        resources = std::move(artifacts->resources);
    }

    auto dependencies = std::move(slang->dependencies);
    auto dependency_snapshots = std::move(slang->dependency_snapshots);
    return ShaderCompileOutput {
        .description =
            ShaderDescription {
                .stage = request.stage,
                .source = std::move(opengl_source),
                .wgsl = std::move(slang->wgsl),
                .spirv = std::move(slang->spirv),
                .path = request.logical_path.string(),
                .resources = std::move(resources),
                .defs = std::move(request.defs),
            },
        .dependencies = std::move(dependencies),
        .dependency_snapshots = std::move(dependency_snapshots),
    };
}

} // namespace fei
