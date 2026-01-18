add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

set_runtimes("MD")
add_requires("catch2", "stb", "glad", "lua", "tinyobjloader")
add_requires("glfw", {configs = {shared = false}})
add_requires("imgui", {configs = {glfw = true, opengl3 = true}})

set_policy("check.auto_ignore_flags", false)

local project_dir = os.scriptdir():gsub("\\", "/")
add_cxxflags("-DFEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")

includes("src")
includes("samples")
includes("tests")
includes("tools")
