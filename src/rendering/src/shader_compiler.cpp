#include "rendering/shader_compiler.hpp"

#include "rendering/shader.hpp"
#include "shader_artifact.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <utility>

#ifdef FEI_HAS_SLANG_SDK
#    include <slang-com-ptr.h>
#    include <slang.h>
#endif

namespace fei {

namespace {

ShaderCompileError shader_compile_error(std::string message) {
    return ShaderCompileError {.message = std::move(message)};
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

    auto source_path = config.shader_sources.resolve(logical_path);
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

#ifdef FEI_HAS_SLANG_SDK
std::string blob_text(slang::IBlob* blob) {
    if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0) {
        return {};
    }
    const auto* data = static_cast<const char*>(blob->getBufferPointer());
    return std::string(data, data + blob->getBufferSize());
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

void append_slang_logical_resource_name(
    std::vector<ShaderArtifactLogicalResourceName>& names,
    std::string name,
    uint32_t set,
    uint32_t binding
) {
    if (name.empty()) {
        return;
    }

    auto it = std::find_if(
        names.begin(),
        names.end(),
        [&](const ShaderArtifactLogicalResourceName& resource) {
            return resource.set == set && resource.binding == binding &&
                   resource.name == name;
        }
    );
    if (it != names.end()) {
        return;
    }

    names.push_back(
        ShaderArtifactLogicalResourceName {
            .name = std::move(name),
            .set = set,
            .binding = binding,
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

void append_slang_parameter_block_logical_resource_names(
    std::vector<ShaderArtifactLogicalResourceName>& names,
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
        append_slang_logical_resource_name(
            names,
            slang_variable_name(parameter),
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

        append_slang_logical_resource_name(
            names,
            slang_variable_name(*field),
            set,
            binding
        );

        auto* field_type_layout = field->getTypeLayout();
        binding += field_type_layout == nullptr ?
                       1 :
                       slang_descriptor_binding_count(*field_type_layout);
    }
}

void append_slang_global_parameter_block_logical_resource_names(
    std::vector<ShaderArtifactLogicalResourceName>& names,
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
        append_slang_parameter_block_logical_resource_names(names, *field);
    }
}

Result<std::vector<ShaderArtifactLogicalResourceName>, ShaderCompileError>
slang_logical_resource_names(slang::IComponentType& linked_program) {
    Slang::ComPtr<slang::IBlob> layout_diagnostics;
    auto* layout = linked_program.getLayout(0, layout_diagnostics.writeRef());
    if (layout == nullptr) {
        return failure(slang_compile_error(
            "Slang failed to generate reflection layout",
            layout_diagnostics
        ));
    }

    std::vector<ShaderArtifactLogicalResourceName> names;
    const auto parameter_count = layout->getParameterCount();
    names.reserve(parameter_count);
    for (unsigned i = 0; i < parameter_count; ++i) {
        auto* parameter = layout->getParameterByIndex(i);
        if (parameter == nullptr) {
            continue;
        }

        if (is_slang_parameter_block(*parameter)) {
            append_slang_parameter_block_logical_resource_names(
                names,
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

        append_slang_logical_resource_name(
            names,
            slang_variable_name(*parameter),
            static_cast<uint32_t>(space),
            static_cast<uint32_t>(binding)
        );
    }

    append_slang_global_parameter_block_logical_resource_names(names, *layout);

    return names;
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
    std::vector<ShaderArtifactLogicalResourceName> logical_resource_names;
    std::vector<std::filesystem::path> dependencies;
};

class TrackingSlangFileSystem final : public ISlangFileSystemExt {
  public:
    SLANG_NO_THROW SlangResult SLANG_MCALL
    queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (outObject == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        if (auto* intf = getInterface(uuid)) {
            addRef();
            *outObject = intf;
            return SLANG_OK;
        }
        *outObject = nullptr;
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
        return getInterface(guid);
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL
    loadFile(char const* path, ISlangBlob** outBlob) override {
        if (path == nullptr || outBlob == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *outBlob = nullptr;

        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return SLANG_E_NOT_FOUND;
        }
        std::string contents {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        *outBlob = slang_createBlob(contents.data(), contents.size());
        if (*outBlob == nullptr) {
            return SLANG_E_OUT_OF_MEMORY;
        }

        insert_unique_dependency(m_dependencies, std::filesystem::path(path));
        return SLANG_OK;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL getFileUniqueIdentity(
        const char* path,
        ISlangBlob** outUniqueIdentity
    ) override {
        if (path == nullptr || outUniqueIdentity == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *outUniqueIdentity = nullptr;
        return make_path_blob(
            canonical_path(std::filesystem::path(path)),
            outUniqueIdentity
        );
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL calcCombinedPath(
        SlangPathType fromPathType,
        const char* fromPath,
        const char* path,
        ISlangBlob** pathOut
    ) override {
        if (fromPath == nullptr || path == nullptr || pathOut == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *pathOut = nullptr;

        auto dependency_path = std::filesystem::path(path);
        if (!dependency_path.is_absolute()) {
            auto base = std::filesystem::path(fromPath);
            if (fromPathType == SLANG_PATH_TYPE_FILE) {
                base = base.parent_path();
            }
            dependency_path = base / dependency_path;
        }
        return make_path_blob(dependency_path.lexically_normal(), pathOut);
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL
    getPathType(const char* path, SlangPathType* pathTypeOut) override {
        if (path == nullptr || pathTypeOut == nullptr) {
            return SLANG_E_INVALID_ARG;
        }

        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if (error || !std::filesystem::exists(status)) {
            return SLANG_E_NOT_FOUND;
        }
        if (std::filesystem::is_directory(status)) {
            *pathTypeOut = SLANG_PATH_TYPE_DIRECTORY;
            return SLANG_OK;
        }
        if (std::filesystem::is_regular_file(status)) {
            *pathTypeOut = SLANG_PATH_TYPE_FILE;
            return SLANG_OK;
        }
        return SLANG_E_NOT_FOUND;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL
    getPath(PathKind kind, const char* path, ISlangBlob** outPath) override {
        if (path == nullptr || outPath == nullptr) {
            return SLANG_E_INVALID_ARG;
        }
        *outPath = nullptr;

        auto result = std::filesystem::path(path);
        switch (kind) {
            case PathKind::Canonical:
            case PathKind::OperatingSystem:
                result = canonical_path(result);
                break;
            case PathKind::Simplified:
            case PathKind::Display:
                result = result.lexically_normal();
                break;
            case PathKind::CountOf:
                return SLANG_E_INVALID_ARG;
        }
        return make_path_blob(result, outPath);
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

  private:
    ISlangUnknown* getInterface(const SlangUUID& uuid) {
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

    static SlangResult
    make_path_blob(const std::filesystem::path& path, ISlangBlob** outBlob) {
        auto string = path.lexically_normal().string();
        *outBlob = slang_createBlob(string.c_str(), string.size() + 1);
        return *outBlob == nullptr ? SLANG_E_OUT_OF_MEMORY : SLANG_OK;
    }

    std::atomic<uint32_t> m_ref_count {0};
    std::vector<std::filesystem::path> m_dependencies;
};

Slang::ComPtr<TrackingSlangFileSystem> make_tracking_slang_file_system() {
    auto* file_system = new TrackingSlangFileSystem;
    file_system->addRef();
    return Slang::ComPtr<TrackingSlangFileSystem>(
        Slang::INIT_ATTACH,
        file_system
    );
}

Result<SlangCompileOutput, ShaderCompileError>
compile_slang_to_spirv(const ShaderCompileRequest& request) {
    std::string source = request.source;
    if (source.empty()) {
        auto file_source = read_text_file(request.source_path);
        if (!file_source) {
            return failure(std::move(file_source).error());
        }
        source = std::move(file_source).value();
    }

    Slang::ComPtr<slang::IGlobalSession> global_session;
    auto result = slang::createGlobalSession(global_session.writeRef());
    if (SLANG_FAILED(result) || !global_session) {
        return failure(
            shader_compile_error("Failed to create Slang global session")
        );
    }

    slang::TargetDesc target_desc {};
    target_desc.format = SLANG_SPIRV;
    target_desc.profile = global_session->findProfile("glsl_450");

    auto macros = make_slang_macro_storage(request.defs);
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
    auto file_system = make_tracking_slang_file_system();

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
            "Slang failed to generate SPIR-V",
            code_diagnostics
        ));
    }

    auto spirv = blob_bytes(code);
    if (!spirv) {
        return failure(std::move(spirv).error());
    }

    auto logical_resource_names = slang_logical_resource_names(*linked_program);
    if (!logical_resource_names) {
        return failure(std::move(logical_resource_names).error());
    }

    std::vector<std::filesystem::path> dependencies;
    insert_unique_dependency(dependencies, request.source_path);
    for (const auto& dependency : file_system->dependencies()) {
        insert_unique_dependency(dependencies, dependency);
    }

    return SlangCompileOutput {
        .spirv = std::move(spirv).value(),
        .logical_resource_names = std::move(logical_resource_names).value(),
        .dependencies = std::move(dependencies),
    };
}

#endif

Result<ShaderArtifactGenerationOutput, ShaderCompileError>
generate_backend_artifacts(ShaderArtifactGenerationInput input) {
    try {
        return generate_shader_artifacts(input);
    } catch (const std::exception& e) {
        return failure(shader_compile_error(
            std::string("Shader artifact generation failed: ") + e.what()
        ));
    }
}

} // namespace

ShaderVariantCompiler::ShaderVariantCompiler(
    ShaderCompiler& compiler,
    RuntimeShaderCompilerConfig config
) :
    m_compiler(&compiler),
    m_config(normalize_runtime_shader_compiler_config(std::move(config))) {}

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
    auto output = m_compiler->compile(compile_request);
    if (!output) {
        return failure(std::move(output).error());
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

#ifdef FEI_HAS_SLANG_SDK
Result<ShaderCompileOutput, ShaderCompileError>
SlangLibraryShaderCompiler::compile(ShaderCompileRequest request) {
    request.defs = normalized_shader_defs(std::move(request.defs));

    auto slang = compile_slang_to_spirv(request);
    if (!slang) {
        return failure(std::move(slang).error());
    }

    auto artifacts = generate_backend_artifacts(
        ShaderArtifactGenerationInput {
            .spirv = slang->spirv,
            .logical_resource_names = slang->logical_resource_names,
        }
    );
    if (!artifacts) {
        return failure(std::move(artifacts).error());
    }

    auto dependencies = std::move(slang->dependencies);
    return ShaderCompileOutput {
        .description =
            ShaderDescription {
                .stage = request.stage,
                .source = std::move(artifacts->opengl_source),
                .spirv = std::move(slang->spirv),
                .path = request.logical_path.string(),
                .resources = std::move(artifacts->resources),
                .defs = std::move(request.defs),
            },
        .dependencies = std::move(dependencies),
    };
}
#endif

} // namespace fei
