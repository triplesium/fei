local slang_wasm_links = {
    "slang-compiler",
    "compiler-core",
    "core",
    "cmark-gfm",
    "miniz",
    "lz4",
}

local function add_slang(target, public)
    if is_plat("wasm") then
        target:add("packages", "fei-slang-wasm", {public = public})
        local linkgroup_args = table.copy(slang_wasm_links)
        table.insert(linkgroup_args, {group = true, public = public})
        target:add("linkgroups", table.unpack(linkgroup_args))
        target:add("cxflags", "-fwasm-exceptions", {force = true, public = public})
        target:add(
            "ldflags",
            "-fwasm-exceptions",
            "-sALLOW_MEMORY_GROWTH",
            {force = true, public = public}
        )
        return
    end

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
    if is_plat("wasm") then
        return
    end

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

local shader_targets = get_config("shader_targets") or ""
local function has_shader_target(name)
    for target_name in shader_targets:gmatch("[^,%s]+") do
        if target_name == name then
            return true
        end
    end
    return false
end

local has_opengl_shader_target = has_shader_target("opengl")
local spirv_cross_sdk = has_opengl_shader_target and find_system_spirv_cross_sdk() or nil
if has_opengl_shader_target and not spirv_cross_sdk then
    local spirv_cross_configs = {shared = true}
    if is_plat("windows") then
        spirv_cross_configs.runtimes = is_mode("debug") and "MDd" or "MD"
    end
    add_requires("spirv-cross", {configs = spirv_cross_configs})
end

target("fei-shader")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("fei-base", "fei-asset", "fei-graphics")
    on_load(function(target)
        add_slang(target, true)
    end)
    before_build(function()
        require_system_slang_sdk()
    end)

target("fei-shader-opengl")
    set_kind("static")
    set_default(has_shader_target("opengl"))
    add_rules("fei.reflect")
    add_headerfiles("backends/opengl/include/**.hpp")
    add_files("backends/opengl/src/*.cpp")
    add_includedirs("backends/opengl/include", {public = true})
    add_deps("fei-shader", "fei-app")
    if spirv_cross_sdk then
        add_includedirs(spirv_cross_sdk.include_dir)
        add_linkdirs(spirv_cross_sdk.lib_dir, {public = true})
        add_links("spirv-cross-c-shared", {public = true})
        add_runenvs("PATH", spirv_cross_sdk.bin_dir)
    elseif has_opengl_shader_target then
        add_packages("spirv-cross", {public = true})
    end

target("fei-shader-vulkan")
    set_kind("static")
    set_default(has_shader_target("vulkan"))
    add_rules("fei.reflect")
    add_headerfiles("backends/vulkan/include/**.hpp")
    add_files("backends/vulkan/src/*.cpp")
    add_includedirs("backends/vulkan/include", {public = true})
    add_deps("fei-shader", "fei-app")

target("fei-shader-webgpu")
    set_kind("static")
    set_default(has_shader_target("webgpu"))
    add_rules("fei.reflect")
    add_headerfiles("backends/webgpu/include/**.hpp")
    add_files("backends/webgpu/src/*.cpp")
    add_includedirs("backends/webgpu/include", {public = true})
    add_deps("fei-shader", "fei-app")

target("fei-shader-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-shader", "fei-shader-opengl")
    on_load(function(target)
        add_slang(target, false)
    end)
