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

add_requires("catch2", "stb", "glad", "lua", "tinyobjloader", "mikktspace", "cpp-httplib", "nlohmann_json")
add_requires("glfw", {configs = {shared = false}})
add_requires("imgui", {configs = {glfw = true, opengl3 = true}})
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
add_defines("FEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")
add_defines("FEI_SHADER_SOURCE_PATH=\"" .. project_dir .. "/src/pbr/shaders\"")
add_defines("FEI_SHADER_ASSETS_PATH=\"" .. project_dir .. "/build/generated/shaders\"")
add_defines("FEI_PROFILE_OUTPUT_PATH=\"" .. project_dir .. "/build/profile/latest\"")

rule("fei.test")
    on_load(function(target)
        target:add("packages", "catch2")
        target:add("tests", "default")
        target:add("deps", "fei-test-support")
    end)
rule_end()

target("fei-test-support")
    set_kind("static")
    set_default(false)
    add_files("tests/support/crt_report.cpp")

add_cxxflags("cl::/Zc:preprocessor")

includes("tools")
includes("src")
includes("samples")
includes("tests")
