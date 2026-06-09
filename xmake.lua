add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

add_requires("catch2", "stb", "glad", "lua", "tinyobjloader", "mikktspace")
add_requires("glfw", {configs = {shared = false}})
add_requires("imgui", {configs = {glfw = true, opengl3 = true}})

set_policy("check.auto_ignore_flags", false)
if is_plat("windows") then
    set_policy("run.windows_error_dialog", false)
end

rule("fei.test")
    on_load(function(target)
        target:add("packages", "catch2")
        target:add("tests", "default")
        target:add("files", path.join(os.projectdir(), "tests/support/crt_report.cpp"))
    end)
rule_end()

local project_dir = os.scriptdir():gsub("\\", "/")
add_cxxflags("-DFEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")

add_cxxflags("cl::/Zc:preprocessor")

includes("src")
includes("samples")
includes("tests")
includes("tools")
