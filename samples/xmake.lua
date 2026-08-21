function add_sample(name, source)
    target("sample-" .. name)
        set_kind("binary")
        add_rules("fei.reflect")
        add_headerfiles("common.hpp")
        add_files((source or name) .. ".cpp")
        add_deps("fei-refl", "fei-ecs", "fei-app", "fei-window", "fei-input", "fei-core", "fei-asset", "fei-graphics-opengl", "fei-graphics-opengl-glfw", "fei-graphics", "fei-rendering", "fei-imgui", "fei-pbr", "fei-scene", "fei-scripting-lua")
        add_packages("glfw", "glad", "imgui", "stb")
end

add_sample("refl")
add_sample("scene")
target("sample-scene")
    add_deps(
        "fei-devtools",
        "fei-devtools-ecs",
        "fei-devtools-input",
        "fei-devtools-pbr",
        "fei-devtools-profiling",
        "fei-devtools-reflection",
        "fei-devtools-rendering",
        "fei-devtools-scripting-lua",
        "fei-gltf",
        "fei-graphics-opengl",
        "fei-graphics-opengl-glfw",
        "fei-graphics-vulkan",
        "fei-graphics-vulkan-glfw"
    )
add_sample("gltf")
target("sample-gltf")
    add_deps(
        "fei-devtools",
        "fei-devtools-ecs",
        "fei-devtools-pbr",
        "fei-gltf",
        "fei-graphics-vulkan",
        "fei-graphics-vulkan-glfw"
    )
add_sample("compute_shader")
add_sample("schedule")
add_sample("registered_system")

target("sample-snapshot-game")
    set_kind("binary")
    add_files("snapshot_game.cpp")
    add_extrafiles("snapshot_game.luau")
    add_deps("fei-snapshot-runtime-luau")

target("sample-physics2d")
    set_kind("binary")
    add_rules("fei.reflect")
    add_files("physics2d.cpp")
    add_deps("fei-app", "fei-core", "fei-physics2d")

add_sample("multithreading")
add_sample("graphics")
add_sample("rendering")
target("sample-rendering")
    add_deps(
        "fei-devtools",
        "fei-devtools-pbr",
        "fei-devtools-rendering",
        "fei-graphics-webgpu",
        "fei-graphics-webgpu-glfw"
    )
add_sample("asset")
add_sample("sprite")
target("sample-sprite")
    add_deps(
        "fei-devtools",
        "fei-devtools-input",
        "fei-devtools-rendering",
        "fei-sprite",
        "fei-graphics-vulkan",
        "fei-graphics-vulkan-glfw",
        "fei-graphics-webgpu",
        "fei-graphics-webgpu-glfw"
    )

add_sample("ui")
target("sample-ui")
    add_deps(
        "fei-devtools",
        "fei-devtools-input",
        "fei-devtools-rendering",
        "fei-input-focus",
        "fei-sprite",
        "fei-text",
        "fei-ui",
        "fei-ui-widgets",
        "fei-ui-rendering"
    )

add_sample("ui-widgets", "ui_widgets")
target("sample-ui-widgets")
    add_deps(
        "fei-devtools",
        "fei-devtools-input",
        "fei-devtools-rendering",
        "fei-input-focus",
        "fei-sprite",
        "fei-text",
        "fei-ui",
        "fei-ui-widgets",
        "fei-ui-rendering"
    )
