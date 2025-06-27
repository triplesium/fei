add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

set_runtimes("MD")
add_requires("catch2", "stb", "glad")
add_requires("glfw", {configs = {shared = false}})

set_policy("check.auto_ignore_flags", false)

local project_dir = os.scriptdir():gsub("\\", "/")
add_cxxflags("-DFEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")

includes("src")
includes("samples")
includes("tests")
