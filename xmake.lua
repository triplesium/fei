add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

option("tracy")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Tracy CPU profiling")
option_end()

option("profile_summary")
    set_default(false)
    set_showmenu(true)
    set_description("Enable engine-side profiling summary output")
option_end()

option("shader_slang_sdk")
    set_default(os.getenv("VULKAN_SDK") or "")
    set_showmenu(true)
    set_description("Path to a Slang SDK used by runtime shader compilation")
option_end()

option("shader_spirv_cross_sdk")
    set_default(os.getenv("VULKAN_SDK") or "")
    set_showmenu(true)
    set_description("Path to a Vulkan/SPIRV-Cross SDK used by shader artifact generation")
option_end()

option("shader_targets")
    set_default("opengl,vulkan,webgpu")
    set_showmenu(true)
    set_description("Comma-separated runtime shader targets to build")
option_end()

includes("packages")

if is_plat("wasm") then
    add_requires("emscripten 6.0.0")
    add_requires(
        "fei-slang-wasm 2026.14.1",
        {configs = {toolchains = "fei-emcc@emscripten"}}
    )
    set_toolchains("fei-emcc@emscripten")
end

if is_plat("wasm") then
    add_requires(
        "catch2",
        "stb",
        "tinyobjloader",
        "mikktspace",
        "nlohmann_json",
        "yaml-cpp"
    )
else
    add_requires("catch2", "stb", "glad", "lua", "tinyobjloader", "mikktspace", "cpp-httplib", "nlohmann_json", "fastgltf v0.9.0")
    add_requires("box2d v3.1.1", {configs = {shared = false}})
    add_requires("luau 696", {configs = {shared = false, extern_c = false}})
    add_requires("yaml-cpp")
    add_requires("glfw", {configs = {shared = false}})
    add_requires("imgui v1.92.7-docking", {configs = {glfw = true, opengl3 = false}})
end
if has_config("tracy") then
    add_requires(
        "tracy v0.13.0",
        {
            configs = {
                on_demand = true,
                enforce_callstack = false,
                callstack = false,
                code_transfer = false,
                context_switch = false,
                broadcast = false,
                sampling = false,
                verify = false,
                vsync_capture = false,
                system_tracing = false,
                frame_image = false,
                fibers = false,
                crash_handler = false,
            }
        }
    )
    if is_plat("windows") then
        add_ldflags("/INCREMENTAL:NO", {force = true})
    end
end

set_policy("check.auto_ignore_flags", false)
if is_plat("windows") then
    set_policy("run.windows_error_dialog", false)
end

local project_dir = os.scriptdir():gsub("\\", "/")
if is_plat("wasm") then
    add_defines("FEI_ASSETS_PATH=\"/fei/assets\"")
else
    add_defines("FEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")
end
add_defines("FEI_SHADER_ASSETS_PATH=\"" .. project_dir .. "/build/generated/shaders\"")
add_defines("FEI_SHADER_CACHE_PATH=\"" .. project_dir .. "/build/cache/shaders\"")
add_defines("FEI_PROFILE_OUTPUT_PATH=\"" .. project_dir .. "/build/profile/latest\"")

local shader_sources = {}

function add_asset_bundle(prefix, root)
    if not prefix or #prefix == 0 then
        raise("asset bundle prefix is required")
    end
    if not root or #root == 0 then
        raise("asset bundle root is required")
    end

    add_values(
        "fei.asset_bundles",
        prefix:gsub("\\", "/") .. "=" ..
            path.absolute(root):gsub("\\", "/")
    )
end

local function target_asset_bundles(target)
    local bundles = {}
    for _, entry in ipairs(table.wrap(target:values("fei.asset_bundles"))) do
        local separator = entry:find("=", 1, true)
        if not separator then
            raise("invalid asset bundle: " .. entry)
        end
        table.insert(bundles, {
            prefix = entry:sub(1, separator - 1),
            root = entry:sub(separator + 1),
        })
    end
    return bundles
end

function add_shader_source(prefix, root)
    if not prefix or #prefix == 0 then
        raise("shader source prefix is required")
    end
    if not root or #root == 0 then
        raise("shader source root is required")
    end

    local normalized_prefix = prefix:gsub("\\", "/")
    local normalized_root = path.absolute(root):gsub("\\", "/")
    table.insert(shader_sources, {
        prefix = normalized_prefix,
        root = normalized_root,
    })
end

local function shader_runtime_root(source, target)
    if target:is_plat("wasm") then
        return "/fei/shaders/" .. source.prefix
    end
    return source.root
end

local function shader_sources_define_value(target)
    table.sort(shader_sources, function(a, b)
        return a.prefix < b.prefix
    end)

    local entries = {}
    for _, source in ipairs(shader_sources) do
        table.insert(
            entries,
            source.prefix .. "=" .. shader_runtime_root(source, target)
        )
    end
    return table.concat(entries, ";")
end

rule("fei.shader_sources")
    after_load(function(target)
        local sources = shader_sources_define_value(target)
        if sources and #sources > 0 then
            target:add("defines", "FEI_SHADER_SOURCES=\"" .. sources .. "\"")
        end
        if target:is_plat("wasm") and target:kind() == "binary" then
            for _, source in ipairs(shader_sources) do
                target:add(
                    "ldflags",
                    "--preload-file=" .. source.root .. "@" ..
                        shader_runtime_root(source, target),
                    {force = true}
                )
                target:add("extrafiles", path.join(source.root, "**.slang"))
            end
        end
    end)
    before_build(function(target)
        if not target:is_plat("wasm") or target:kind() ~= "binary" then
            return
        end

        import("core.project.depend")
        local source_files = {}
        for _, source in ipairs(shader_sources) do
            table.join2(
                source_files,
                os.files(path.join(source.root, "**.slang"))
            )
        end
        table.sort(source_files)

        depend.on_changed(function()
            os.rm(target:targetfile())
        end, {
            dependfile = target:dependfile(
                path.join(target:autogendir(), "shader_sources")
            ),
            files = source_files,
            values = source_files,
        })
    end)
rule_end()

add_rules("fei.shader_sources")

rule("fei.asset_bundles")
    after_load(function(target)
        if not target:is_plat("wasm") or target:kind() ~= "binary" then
            return
        end

        for _, bundle in ipairs(target_asset_bundles(target)) do
            target:add(
                "ldflags",
                "--preload-file=" .. bundle.root .. "@/fei/assets/" ..
                    bundle.prefix,
                {force = true}
            )
            target:add("extrafiles", path.join(bundle.root, "**"))
        end
    end)
    before_build(function(target)
        if not target:is_plat("wasm") or target:kind() ~= "binary" then
            return
        end

        import("core.project.depend")
        local asset_files = {}
        for _, bundle in ipairs(target_asset_bundles(target)) do
            table.join2(asset_files, os.files(path.join(bundle.root, "**")))
        end
        table.sort(asset_files)

        depend.on_changed(function()
            os.rm(target:targetfile())
        end, {
            dependfile = target:dependfile(
                path.join(target:autogendir(), "asset_bundles")
            ),
            files = asset_files,
            values = asset_files,
        })
    end)
rule_end()

add_rules("fei.asset_bundles")

rule("fei.executable_startup")
    on_load(function(target)
        if target:kind() == "binary" then
            target:add(
                "files",
                path.join(
                    os.projectdir(),
                    "engine/base/src/startup/crt_report.cpp"
                )
            )
        end
    end)
rule_end()

add_rules("fei.executable_startup")

rule("fei.test")
    on_load(function(target)
        target:add("packages", "catch2")
        target:add("tests", "default")
    end)
rule_end()

add_cxxflags("cl::/Zc:preprocessor")

if is_plat("wasm") then
    includes("tools/reflgen")
    includes("engine")
    includes("samples/browser")
else
    includes("tools")
    includes("engine")
    includes("samples")
    includes("tests")
end
