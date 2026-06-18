add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

add_requires("catch2", "stb", "glad", "lua", "tinyobjloader", "mikktspace", "cpp-httplib", "nlohmann_json")
add_requires("glfw", {configs = {shared = false}})
add_requires("imgui", {configs = {glfw = true, opengl3 = true}})

set_policy("check.auto_ignore_flags", false)
if is_plat("windows") then
    set_policy("run.windows_error_dialog", false)
end

local project_dir = os.scriptdir():gsub("\\", "/")
add_defines("FEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")
add_defines("FEI_SHADER_ASSETS_PATH=\"" .. project_dir .. "/build/generated/shaders\"")

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
