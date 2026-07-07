#include "rendering/shader_compiler.hpp"

#include "rendering/shader.hpp"
#include "shadergen/shadergen.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>
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

std::filesystem::path default_runtime_shader_source_root() {
#ifdef FEI_SHADER_SOURCE_PATH
    return std::filesystem::path(FEI_SHADER_SOURCE_PATH);
#else
    return std::filesystem::current_path() / "src" / "pbr" / "shaders";
#endif
}

std::filesystem::path default_runtime_shader_output_root() {
#ifdef FEI_SHADER_ASSETS_PATH
    return std::filesystem::path(FEI_SHADER_ASSETS_PATH) / "runtime";
#else
    return std::filesystem::current_path() / "build" / "generated" / "shaders" /
           "runtime";
#endif
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

std::filesystem::path
depfile_logical_path(const ShaderCompileRequest& request) {
    if (!request.dependency_path.empty()) {
        return request.dependency_path;
    }
    if (request.source_path.empty()) {
        return request.logical_path;
    }
    auto relative = request.source_path.lexically_relative(request.source_root);
    if (!relative.empty()) {
        return relative;
    }
    return request.logical_path;
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

std::filesystem::path relative_source_path(
    const std::filesystem::path& source_root,
    const std::filesystem::path& source_path
) {
    auto relative = source_path.lexically_relative(source_root);
    if (!relative.empty()) {
        return relative;
    }
    return source_path.filename();
}

std::string shader_defs_fingerprint(ShaderDefs defs) {
    defs = normalized_shader_defs(std::move(defs));
    if (defs.empty()) {
        return "default";
    }

    std::uint64_t hash = 1469598103934665603ull;
    auto feed = [&](std::string_view text) {
        for (char c : text) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
    };

    for (const auto& def : defs) {
        feed(def.name);
        feed(std::string_view("\0", 1));
        feed(std::to_string(def.value.index()));
        feed(std::string_view("\0", 1));
        feed(shader_def_define_value(def.value));
        feed(std::string_view("\0", 1));
    }

    std::ostringstream output;
    output << "defs-" << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

RuntimeShaderCompilerConfig
normalize_runtime_shader_compiler_config(RuntimeShaderCompilerConfig config) {
    if (config.source_root.empty()) {
        config.source_root = default_runtime_shader_source_root();
    }
    if (config.output_root.empty()) {
        config.output_root = default_runtime_shader_output_root();
    }
    return config;
}

Result<std::filesystem::path, ShaderCompileError> resolve_shader_source_path(
    const RuntimeShaderCompilerConfig& config,
    const std::filesystem::path& logical_path
) {
    auto stage_specific_slang = with_suffix(logical_path, ".slang");
    auto base_slang = logical_path;
    base_slang.replace_extension(".slang");

    std::vector<std::filesystem::path> candidates {
        stage_specific_slang,
        base_slang,
    };
    candidates.erase(
        std::unique(candidates.begin(), candidates.end()),
        candidates.end()
    );

    for (const auto& candidate : candidates) {
        auto source_path = config.source_root / candidate;
        if (std::filesystem::exists(source_path)) {
            return source_path.lexically_normal();
        }
    }

    return failure(shader_compile_error(
        "Runtime Slang shader source not found for " + logical_path.string() +
        "; expected " + stage_specific_slang.string() + " or " +
        base_slang.string() + " under " + config.source_root.string()
    ));
}

Result<ShaderCompileRequest, ShaderCompileError>
make_runtime_shader_compile_request(
    const RuntimeShaderCompilerConfig& config,
    std::filesystem::path logical_path,
    ShaderDefs defs
) {
    logical_path = logical_path.lexically_normal();
    auto stage = shader_stage_from_path(logical_path);
    if (!stage) {
        return failure(shader_compile_error(
            "Unsupported shader logical path: " + logical_path.string()
        ));
    }

    auto source_path = resolve_shader_source_path(config, logical_path);
    if (!source_path) {
        return failure(std::move(source_path).error());
    }

    auto normalized_defs = normalized_shader_defs(std::move(defs));
    auto relative_source =
        relative_source_path(config.source_root, source_path.value());
    auto stage_name = shader_stage_name(stage.value());
    auto entry = std::string {"main"};
    auto dependency_path = relative_source;
    if (strip_slang_suffix(relative_source).generic_string() !=
        logical_path.generic_string()) {
        entry = shader_stage_entry_name(stage.value());
        dependency_path += "." + stage_name;
    }

    return ShaderCompileRequest {
        .language = ShaderSourceLanguage::Slang,
        .source_path = std::move(source_path).value(),
        .source_root = config.source_root,
        .logical_path = std::move(logical_path),
        .output_root =
            config.output_root / shader_defs_fingerprint(normalized_defs),
        .dependency_path = std::move(dependency_path),
        .stage = stage.value(),
        .entry = std::move(entry),
        .defs = std::move(normalized_defs),
    };
}

void append_define_args(std::vector<std::string>& args, ShaderDefs defs) {
    for (const auto& def : normalized_shader_defs(std::move(defs))) {
        args.push_back(shader_def_define_argument(def));
    }
}

Result<ShaderCompilerInvocation, ShaderCompileError> make_source_invocation(
    const ShaderCompileRequest& request,
    const ShaderCompileArtifacts& artifacts,
    const ShaderCompileTools& tools
) {
    auto stage = shader_stage_name(request.stage);
    if (stage.empty()) {
        return failure(shader_compile_error("Unsupported shader stage"));
    }

    std::vector<std::string> args;
    auto program = request.language == ShaderSourceLanguage::Slang ?
                       tools.slangc :
                       tools.glslc;
    if (program.empty()) {
        return failure(shader_compile_error(
            request.language == ShaderSourceLanguage::Slang ?
                "Missing slangc path" :
                "Missing glslc path"
        ));
    }

    if (request.language == ShaderSourceLanguage::Slang) {
        args = {
            "-target",
            "spirv",
            "-profile",
            "glsl_450",
            "-matrix-layout-row-major",
            "-entry",
            request.entry,
            "-stage",
            stage,
            "-I",
            request.source_root.string(),
        };
        append_define_args(args, request.defs);
        args.insert(
            args.end(),
            {
                "-depfile",
                artifacts.depfile_path.string(),
                "-reflection-json",
                artifacts.slang_reflection_path.string(),
                "-o",
                artifacts.spirv_path.string(),
                request.source_path.string(),
            }
        );
    } else {
        args = {
            "--target-env=vulkan1.1",
            "-I",
            request.source_root.string(),
        };
        append_define_args(args, request.defs);
        args.insert(
            args.end(),
            {
                "-MD",
                "-MF",
                artifacts.depfile_path.string(),
                "-MT",
                request.logical_path.generic_string() + ".spv",
                "-o",
                artifacts.spirv_path.string(),
                request.source_path.string(),
            }
        );
    }

    return ShaderCompilerInvocation {
        .program = std::move(program),
        .args = std::move(args),
    };
}

Result<ShaderCompilerInvocation, ShaderCompileError> make_shadergen_invocation(
    const ShaderCompileRequest& request,
    const ShaderCompileArtifacts& artifacts,
    const ShaderCompileTools& tools
) {
    if (tools.shadergen.empty()) {
        return failure(shader_compile_error("Missing shadergen path"));
    }

    std::vector<std::string> args {
        "--input",
        artifacts.spirv_path.string(),
        "--glsl",
        artifacts.opengl_path.string(),
        "--reflect",
        artifacts.reflection_path.string(),
    };
    if (request.language == ShaderSourceLanguage::Slang) {
        args.push_back("--slang-reflect");
        args.push_back(artifacts.slang_reflection_path.string());
    }

    return ShaderCompilerInvocation {
        .program = tools.shadergen,
        .args = std::move(args),
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

std::string trim_ascii(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

Optional<std::filesystem::path> parse_include_path(std::string_view line) {
    auto hash = line.find('#');
    if (hash == std::string_view::npos) {
        return nullopt;
    }
    line.remove_prefix(hash + 1);
    auto trimmed = trim_ascii(line);
    line = std::string_view(trimmed);
    constexpr std::string_view Include = "include";
    if (!line.starts_with(Include)) {
        return nullopt;
    }
    line.remove_prefix(Include.size());
    trimmed = trim_ascii(line);
    line = std::string_view(trimmed);
    if (line.empty() || (line.front() != '"' && line.front() != '<')) {
        return nullopt;
    }

    const char close = line.front() == '"' ? '"' : '>';
    line.remove_prefix(1);
    auto end = line.find(close);
    if (end == std::string_view::npos) {
        return nullopt;
    }
    return std::filesystem::path(std::string(line.substr(0, end)));
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

Optional<std::filesystem::path> resolve_include_path(
    const std::filesystem::path& include_path,
    const std::filesystem::path& includer,
    const std::filesystem::path& source_root
) {
    std::vector<std::filesystem::path> candidates;
    if (include_path.is_absolute()) {
        candidates.push_back(include_path);
    } else {
        candidates.push_back(includer.parent_path() / include_path);
        if (!source_root.empty()) {
            candidates.push_back(source_root / include_path);
        }
    }

    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return normalized_absolute_path(candidate);
        }
    }
    return nullopt;
}

void collect_source_dependencies_recursive(
    const std::filesystem::path& source_path,
    const std::filesystem::path& source_root,
    std::unordered_set<std::string>& visited,
    std::vector<std::filesystem::path>& dependencies
) {
    auto normalized = normalized_absolute_path(source_path);
    auto key = normalized.generic_string();
    if (!visited.insert(key).second) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(normalized, error)) {
        return;
    }
    insert_unique_dependency(dependencies, normalized);

    auto source = read_text_file(normalized);
    if (!source) {
        return;
    }

    std::istringstream input(source.value());
    std::string line;
    while (std::getline(input, line)) {
        auto include_path = parse_include_path(line);
        if (!include_path) {
            continue;
        }
        auto resolved =
            resolve_include_path(include_path.value(), normalized, source_root);
        if (!resolved) {
            continue;
        }
        collect_source_dependencies_recursive(
            resolved.value(),
            source_root,
            visited,
            dependencies
        );
    }
}

std::vector<std::filesystem::path> collect_source_dependencies(
    const std::filesystem::path& source_path,
    const std::filesystem::path& source_root
) {
    std::unordered_set<std::string> visited;
    std::vector<std::filesystem::path> dependencies;
    collect_source_dependencies_recursive(
        source_path,
        normalized_absolute_path(source_root),
        visited,
        dependencies
    );
    return dependencies;
}

std::vector<std::filesystem::path>
parse_make_depfile(const std::filesystem::path& depfile_path) {
    std::vector<std::filesystem::path> dependencies;
    auto content = read_text_file(depfile_path);
    if (!content) {
        return dependencies;
    }

    std::string normalized;
    normalized.reserve(content->size());
    for (std::size_t index = 0; index < content->size(); ++index) {
        if ((*content)[index] == '\\' && index + 1 < content->size() &&
            ((*content)[index + 1] == '\n' || (*content)[index + 1] == '\r')) {
            ++index;
            if ((*content)[index] == '\r' && index + 1 < content->size() &&
                (*content)[index + 1] == '\n') {
                ++index;
            }
            normalized.push_back(' ');
        } else {
            normalized.push_back((*content)[index]);
        }
    }

    auto colon = normalized.find(':');
    if (colon == std::string::npos) {
        return dependencies;
    }
    auto body = normalized.substr(colon + 1);
    std::string token;
    bool escaping = false;
    auto flush_token = [&] {
        if (token.empty()) {
            return;
        }
        std::error_code error;
        if (std::filesystem::is_regular_file(token, error)) {
            insert_unique_dependency(dependencies, token);
        }
        token.clear();
    };

    for (char c : body) {
        if (escaping) {
            token.push_back(c);
            escaping = false;
            continue;
        }
        if (c == '\\') {
            escaping = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            flush_token();
        } else {
            token.push_back(c);
        }
    }
    flush_token();
    return dependencies;
}

std::vector<std::filesystem::path> shader_compile_dependencies(
    const ShaderCompileRequest& request,
    const ShaderCompileArtifacts& artifacts
) {
    auto dependencies =
        collect_source_dependencies(request.source_path, request.source_root);
    for (const auto& depfile_dependency :
         parse_make_depfile(artifacts.depfile_path)) {
        insert_unique_dependency(dependencies, depfile_dependency);
    }
    return dependencies;
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

Status<ShaderCompileError>
write_blob_file(const std::filesystem::path& path, slang::IBlob* blob) {
    if (!blob) {
        return failure(
            shader_compile_error("Slang returned an empty code blob")
        );
    }

    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return failure(shader_compile_error(
            "Failed to open shader output: " + path.string()
        ));
    }

    output.write(
        static_cast<const char*>(blob->getBufferPointer()),
        static_cast<std::streamsize>(blob->getBufferSize())
    );
    if (!output) {
        return failure(shader_compile_error(
            "Failed to write shader output: " + path.string()
        ));
    }
    return {};
}

Status<ShaderCompileError> write_slang_reflection_json(
    slang::IComponentType& linked_program,
    const std::filesystem::path& path
) {
    Slang::ComPtr<slang::IBlob> layout_diagnostics;
    auto* layout = linked_program.getLayout(0, layout_diagnostics.writeRef());
    if (layout == nullptr) {
        return failure(slang_compile_error(
            "Slang failed to generate reflection layout",
            layout_diagnostics
        ));
    }

    Slang::ComPtr<slang::IBlob> reflection;
    auto result = layout->toJson(reflection.writeRef());
    if (SLANG_FAILED(result) || !reflection) {
        return failure(
            shader_compile_error("Slang failed to generate reflection JSON")
        );
    }

    return write_blob_file(path, reflection);
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

Status<ShaderCompileError> compile_slang_to_spirv(
    const ShaderCompileRequest& request,
    const ShaderCompileArtifacts& artifacts
) {
    if (request.language != ShaderSourceLanguage::Slang) {
        return failure(shader_compile_error(
            "SlangLibraryShaderCompiler only supports Slang"
        ));
    }

    auto source = read_text_file(request.source_path);
    if (!source) {
        return failure(std::move(source).error());
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
    auto search_path = request.source_root.string();
    const char* search_paths[] = {search_path.c_str()};

    slang::SessionDesc session_desc {};
    session_desc.targets = &target_desc;
    session_desc.targetCount = 1;
    session_desc.searchPaths = search_path.empty() ? nullptr : search_paths;
    session_desc.searchPathCount = search_path.empty() ? 0 : 1;
    session_desc.preprocessorMacros = macros.macros.data();
    session_desc.preprocessorMacroCount =
        static_cast<SlangInt>(macros.macros.size());
    session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

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
        source->c_str(),
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

    auto spirv_status = write_blob_file(artifacts.spirv_path, code);
    if (!spirv_status) {
        return failure(std::move(spirv_status).error());
    }

    return write_slang_reflection_json(
        *linked_program,
        artifacts.slang_reflection_path
    );
}

Status<ShaderCompileError>
generate_backend_artifacts(const ShaderCompileArtifacts& artifacts) {
    try {
        auto slang_reflection_path =
            std::filesystem::exists(artifacts.slang_reflection_path) ?
                artifacts.slang_reflection_path :
                std::filesystem::path {};
        shadergen::generate_shader_artifacts(
            shadergen::ShaderArtifactGenerationRequest {
                .spirv_path = artifacts.spirv_path,
                .opengl_path = artifacts.opengl_path,
                .reflection_path = artifacts.reflection_path,
                .slang_reflection_path = std::move(slang_reflection_path),
            }
        );
        return {};
    } catch (const std::exception& e) {
        return failure(
            shader_compile_error(std::string("Shadergen failed: ") + e.what())
        );
    }
}
#endif

} // namespace

std::string shader_stage_name(ShaderStages stage) {
    switch (stage) {
        case ShaderStages::Vertex:
            return "vertex";
        case ShaderStages::Geometry:
            return "geometry";
        case ShaderStages::Fragment:
            return "fragment";
        case ShaderStages::Compute:
            return "compute";
        default:
            return {};
    }
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

std::string shader_def_define_argument(const ShaderDefVal& def) {
    return "-D" + def.name + "=" + shader_def_define_value(def.value);
}

ShaderCompileArtifacts
shader_compile_artifact_paths(const ShaderCompileRequest& request) {
    auto dep_path = depfile_logical_path(request);
    return ShaderCompileArtifacts {
        .spirv_path = with_suffix(
            request.output_root / "vulkan" / request.logical_path,
            ".spv"
        ),
        .opengl_path = request.output_root / "opengl" / request.logical_path,
        .reflection_path = with_suffix(
            request.output_root / "reflection" / request.logical_path,
            ".json"
        ),
        .slang_reflection_path =
            with_suffix(request.output_root / "reflection" / dep_path, ".json"),
        .depfile_path =
            with_suffix(request.output_root / "deps" / dep_path, ".mk.d"),
    };
}

Result<ShaderCompilePlan, ShaderCompileError> make_shader_compile_plan(
    ShaderCompileRequest request,
    const ShaderCompileTools& tools
) {
    request.defs = normalized_shader_defs(std::move(request.defs));
    auto artifacts = shader_compile_artifact_paths(request);

    auto source_invocation = make_source_invocation(request, artifacts, tools);
    if (!source_invocation) {
        return failure(std::move(source_invocation).error());
    }

    auto shadergen_invocation =
        make_shadergen_invocation(request, artifacts, tools);
    if (!shadergen_invocation) {
        return failure(std::move(shadergen_invocation).error());
    }

    std::vector<ShaderCompilerInvocation> invocations;
    invocations.push_back(std::move(source_invocation).value());
    invocations.push_back(std::move(shadergen_invocation).value());
    return ShaderCompilePlan {
        .artifacts = std::move(artifacts),
        .invocations = std::move(invocations),
    };
}

Result<ShaderCompilePlan, ShaderCompileError> make_shadergen_compile_plan(
    ShaderCompileRequest request,
    const ShaderCompileTools& tools
) {
    request.defs = normalized_shader_defs(std::move(request.defs));
    auto artifacts = shader_compile_artifact_paths(request);

    auto shadergen_invocation =
        make_shadergen_invocation(request, artifacts, tools);
    if (!shadergen_invocation) {
        return failure(std::move(shadergen_invocation).error());
    }

    std::vector<ShaderCompilerInvocation> invocations;
    invocations.push_back(std::move(shadergen_invocation).value());
    return ShaderCompilePlan {
        .artifacts = std::move(artifacts),
        .invocations = std::move(invocations),
    };
}

ExternalShaderCompiler::ExternalShaderCompiler(
    ShaderCompileTools tools,
    ShaderCompilerCommandRunner runner
) : m_tools(std::move(tools)), m_runner(std::move(runner)) {}

Result<ShaderCompileOutput, ShaderCompileError>
ExternalShaderCompiler::compile(ShaderCompileRequest request) {
    if (!m_runner) {
        return failure(shader_compile_error(
            "ExternalShaderCompiler requires a command runner"
        ));
    }

    auto dependency_request = request;
    auto plan = make_shader_compile_plan(std::move(request), m_tools);
    if (!plan) {
        return failure(std::move(plan).error());
    }

    for (const auto& invocation : plan->invocations) {
        auto status = m_runner(invocation);
        if (!status) {
            return failure(std::move(status).error());
        }
    }

    auto dependencies =
        shader_compile_dependencies(dependency_request, plan->artifacts);
    return ShaderCompileOutput {
        .artifacts = std::move(plan->artifacts),
        .dependencies = std::move(dependencies),
    };
}

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
    if (m_compiler == nullptr) {
        return failure(shader_compile_error(
            "ShaderVariantCompiler requires a ShaderCompiler"
        ));
    }

    auto request = make_runtime_shader_compile_request(
        m_config,
        std::move(logical_path),
        std::move(defs)
    );
    if (!request) {
        return failure(std::move(request).error());
    }

    auto compile_request = std::move(request).value();
    auto dependency_request = compile_request;
    auto compiled_logical_path = compile_request.logical_path;
    auto normalized_defs = compile_request.defs;
    auto output = m_compiler->compile(std::move(compile_request));
    if (!output) {
        return failure(std::move(output).error());
    }

    auto dependencies = std::move(output->dependencies);
    for (const auto& dependency :
         shader_compile_dependencies(dependency_request, output->artifacts)) {
        insert_unique_dependency(dependencies, dependency);
    }

    auto description = load_compiled_shader_description(
        std::move(compiled_logical_path),
        CompiledShaderArtifactPaths {
            .opengl_path = output->artifacts.opengl_path,
            .spirv_path = output->artifacts.spirv_path,
            .reflection_path = output->artifacts.reflection_path,
        },
        std::move(normalized_defs)
    );
    if (!description) {
        return failure(shader_compile_error(
            "Failed to load runtime shader artifacts: " + description.error()
        ));
    }

    return ShaderVariantCompileOutput {
        .description = std::move(description).value(),
        .dependencies = std::move(dependencies),
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

#ifdef FEI_HAS_SLANG_SDK
Result<ShaderCompileOutput, ShaderCompileError>
SlangLibraryShaderCompiler::compile(ShaderCompileRequest request) {
    request.defs = normalized_shader_defs(std::move(request.defs));
    auto artifacts = shader_compile_artifact_paths(request);

    auto spirv_status = compile_slang_to_spirv(request, artifacts);
    if (!spirv_status) {
        return failure(std::move(spirv_status).error());
    }

    auto backend_status = generate_backend_artifacts(artifacts);
    if (!backend_status) {
        return failure(std::move(backend_status).error());
    }

    auto dependencies = shader_compile_dependencies(request, artifacts);
    return ShaderCompileOutput {
        .artifacts = std::move(artifacts),
        .dependencies = std::move(dependencies),
    };
}
#endif

} // namespace fei
