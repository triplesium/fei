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
        target:add("defines", "FEI_HAS_SLANG_SDK=1", {public = true})
        target:add("linkdirs", lib_dir, {public = true})
        target:add("links", "slang", {public = true})
    else
        target:add("defines", "FEI_HAS_SLANG_SDK=1")
        target:add("linkdirs", lib_dir)
        target:add("links", "slang")
    end
    target:add("includedirs", include_dir)
    target:add("runenvs", "PATH", bin_dir)
end

target("fei-rendering")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp", "src/mesh/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("fei-base", "fei-refl", "fei-ecs", "fei-app", "fei-math", "fei-asset", "fei-core", "fei-graphics", "fei-profiling", "fei-shadergen-core")
    add_packages("tinyobjloader", "mikktspace", "nlohmann_json")
    on_load(function(target)
        add_system_slang_sdk(target, true)
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
