local function add_system_slang_sdk(target, public)
    local slang_sdk = get_config("shader_slang_sdk")
    if not slang_sdk or #slang_sdk == 0 then
        return
    end

    local include_dir = path.join(slang_sdk, "Include", "slang")
    local lib_dir = path.join(slang_sdk, "Lib")
    local bin_dir = path.join(slang_sdk, "Bin")
    local header = path.join(include_dir, "slang.h")
    local import_lib = path.join(lib_dir, "slang.lib")
    local runtime = path.join(bin_dir, "slang.dll")
    if not os.isfile(header) or not os.isfile(import_lib) or not os.isfile(runtime) then
        return
    end

    if public then
        target:add("linkdirs", lib_dir, {public = true})
        target:add("links", "slang", {public = true})
    else
        target:add("linkdirs", lib_dir)
        target:add("links", "slang")
    end
    target:add("includedirs", include_dir)
    target:add("runenvs", "PATH", bin_dir)
end

local function require_system_slang_sdk()
    local slang_sdk = get_config("shader_slang_sdk")
    if not slang_sdk or #slang_sdk == 0 then
        os.raise("Slang SDK is required; configure shader_slang_sdk with the SDK path")
    end

    local header = path.join(slang_sdk, "Include", "slang", "slang.h")
    local import_lib = path.join(slang_sdk, "Lib", "slang.lib")
    local runtime = path.join(slang_sdk, "Bin", "slang.dll")
    if not os.isfile(header) or not os.isfile(import_lib) or not os.isfile(runtime) then
        os.raise(
            "Invalid Slang SDK at '%s'; expected Include/slang/slang.h, Lib/slang.lib, and Bin/slang.dll",
            slang_sdk
        )
    end
end

local function find_system_spirv_cross_sdk()
    local sdk = get_config("shader_spirv_cross_sdk")
    if not sdk or #sdk == 0 then
        return nil
    end

    local include_dir = path.join(sdk, "Include")
    local lib_dir = path.join(sdk, "Lib")
    local bin_dir = path.join(sdk, "Bin")
    local header = path.join(include_dir, "spirv_cross", "spirv_cross_c.h")
    local import_lib = path.join(lib_dir, "spirv-cross-c-shared.lib")
    local runtime = path.join(bin_dir, "spirv-cross-c-shared.dll")
    if os.isfile(header) and os.isfile(import_lib) and os.isfile(runtime) then
        return {
            include_dir = include_dir,
            lib_dir = lib_dir,
            bin_dir = bin_dir
        }
    end
    return nil
end

local spirv_cross_sdk = find_system_spirv_cross_sdk()
if not spirv_cross_sdk then
    local spirv_cross_configs = {}
    spirv_cross_configs.shared = true
    if is_plat("windows") then
        spirv_cross_configs.runtimes = is_mode("debug") and "MDd" or "MD"
    end
    add_requires("spirv-cross", {configs = spirv_cross_configs})
end

target("fei-rendering")
    set_kind("static")
    add_shader_source("rendering", path.join(os.scriptdir(), "shaders"))
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp", "src/mesh/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("fei-base", "fei-refl", "fei-ecs", "fei-app", "fei-math", "fei-asset", "fei-core", "fei-graphics", "fei-profiling")
    add_packages("tinyobjloader", "mikktspace")
    if spirv_cross_sdk then
        add_includedirs(spirv_cross_sdk.include_dir, {public = true})
        add_linkdirs(spirv_cross_sdk.lib_dir, {public = true})
        add_links("spirv-cross-c-shared", {public = true})
        add_runenvs("PATH", spirv_cross_sdk.bin_dir)
    else
        add_packages("spirv-cross", {public = true})
    end
    on_load(function(target)
        add_system_slang_sdk(target, true)
    end)
    before_build(function()
        require_system_slang_sdk()
    end)

target("fei-rendering-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-rendering")
    on_load(function(target)
        add_system_slang_sdk(target, false)
    end)
