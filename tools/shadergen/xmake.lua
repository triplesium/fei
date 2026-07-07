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

add_requires("cli11", "nlohmann_json")
if not spirv_cross_sdk then
    local spirv_cross_configs = {}
    spirv_cross_configs.shared = true
    if is_plat("windows") then
        spirv_cross_configs.runtimes = is_mode("debug") and "MDd" or "MD"
    end
    add_requires("spirv-cross", {configs = spirv_cross_configs})
end

target("fei-shadergen-core")
    set_kind("static")
    set_default(false)
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_packages("nlohmann_json", {public = true})
    if spirv_cross_sdk then
        add_includedirs(spirv_cross_sdk.include_dir, {public = true})
        add_linkdirs(spirv_cross_sdk.lib_dir, {public = true})
        add_links("spirv-cross-c-shared", {public = true})
        add_runenvs("PATH", spirv_cross_sdk.bin_dir)
    else
        add_packages("spirv-cross", {public = true})
    end

target("fei-shadergen")
    set_kind("binary")
    set_default(false)
    set_policy("build.fence", true)
    add_files("main.cpp")
    add_deps("fei-shadergen-core")
    add_packages("cli11")
    if spirv_cross_sdk then
        add_runenvs("PATH", spirv_cross_sdk.bin_dir)
    end

rule("fei.shader")
    set_extensions(".vert", ".geom", ".frag", ".comp", ".slang")

    on_load(function(target)
        target:add("deps", "fei-shadergen", {links = false})
    end)

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("shadergen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).buildcmd_file(target, batchcmds, sourcefile, opt)
    end)
rule_end()
