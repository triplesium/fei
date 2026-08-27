function add_sample(name, source)
    target("sample-" .. name)
        set_kind("binary")
        add_rules("entisium.reflect")
        add_headerfiles("common.hpp")
        add_files((source or name) .. ".cpp")
        add_deps("entisium-refl", "entisium-ecs", "entisium-app", "entisium-window-glfw", "entisium-input", "entisium-core", "entisium-asset", "entisium-graphics-opengl", "entisium-graphics-opengl-glfw", "entisium-graphics", "entisium-rendering", "entisium-imgui", "entisium-pbr", "entisium-scene", "entisium-scripting-lua")
        add_packages("glfw", "glad", "imgui", "stb")
end

add_sample("refl")
add_sample("scene")
target("sample-scene")
    add_deps(
        "entisium-devtools",
        "entisium-devtools-ecs",
        "entisium-devtools-input",
        "entisium-devtools-pbr",
        "entisium-devtools-profiling",
        "entisium-devtools-reflection",
        "entisium-devtools-rendering",
        "entisium-devtools-scripting-lua",
        "entisium-gltf",
        "entisium-graphics-opengl",
        "entisium-graphics-opengl-glfw",
        "entisium-graphics-vulkan",
        "entisium-graphics-vulkan-glfw"
    )
add_sample("gltf")
target("sample-gltf")
    add_deps(
        "entisium-devtools",
        "entisium-devtools-ecs",
        "entisium-devtools-pbr",
        "entisium-gltf",
        "entisium-graphics-vulkan",
        "entisium-graphics-vulkan-glfw"
    )
add_sample("compute_shader")
add_sample("schedule")
add_sample("registered_system")

target("sample-snapshot-game")
    set_kind("binary")
    add_rules("entisium.reflect")
    add_files("snapshot_game.cpp")
    add_extrafiles("snapshot_game.luau")
    add_deps("entisium-snapshot-runtime-luau")

target("sample-physics2d")
    set_kind("binary")
    add_rules("entisium.reflect")
    add_files("physics2d.cpp")
    add_deps("entisium-app", "entisium-core", "entisium-physics2d")

add_sample("multithreading")
add_sample("graphics")
add_sample("rendering")
target("sample-rendering")
    add_deps(
        "entisium-devtools",
        "entisium-devtools-pbr",
        "entisium-devtools-rendering",
        "entisium-graphics-webgpu",
        "entisium-graphics-webgpu-glfw"
    )
add_sample("asset")
add_sample("sprite")
target("sample-sprite")
    add_deps(
        "entisium-devtools",
        "entisium-devtools-input",
        "entisium-devtools-rendering",
        "entisium-sprite",
        "entisium-graphics-vulkan",
        "entisium-graphics-vulkan-glfw",
        "entisium-graphics-webgpu",
        "entisium-graphics-webgpu-glfw"
    )

add_sample("ui")
target("sample-ui")
    add_deps(
        "entisium-devtools",
        "entisium-devtools-input",
        "entisium-devtools-rendering",
        "entisium-input-focus",
        "entisium-sprite",
        "entisium-text",
        "entisium-ui",
        "entisium-ui-widgets",
        "entisium-ui-rendering"
    )

add_sample("ui-widgets", "ui_widgets")
target("sample-ui-widgets")
    add_deps(
        "entisium-devtools",
        "entisium-devtools-input",
        "entisium-devtools-rendering",
        "entisium-input-focus",
        "entisium-sprite",
        "entisium-text",
        "entisium-ui",
        "entisium-ui-widgets",
        "entisium-ui-rendering"
    )
