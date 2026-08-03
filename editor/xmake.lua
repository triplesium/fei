target("fei-editor-core")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files(
        "src/activity.cpp",
        "src/asset_browser.cpp",
        "src/asset_watcher.cpp",
        "src/component_operations.cpp",
        "src/plugin.cpp"
    )
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-refl",
        "fei-serialization",
        "fei-ecs",
        "fei-app",
        "fei-math",
        "fei-asset",
        "fei-project",
        "fei-scene",
        "fei-core",
        "fei-graphics",
        "fei-rendering",
        "fei-sprite",
        "fei-imgui"
    )
    add_packages("imgui", {public = true})

target("fei-editor")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_rules("fei.reflect")
    add_files("src/main.cpp")
    add_deps(
        "fei-editor-core",
        "fei-project",
        "fei-graphics-opengl",
        "fei-graphics-opengl-glfw"
    )
    add_packages("glfw", "glad", "imgui")

target("fei-editor-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-editor-core")
