function add_sample(name)
    target("sample-" .. name)
        set_kind("binary")
        add_rules("fei.reflect")
        add_headerfiles("common.hpp")
        add_files(name .. ".cpp")
        add_deps("fei-refl", "fei-ecs", "fei-app", "fei-window", "fei-core", "fei-asset", "fei-graphics-opengl", "fei-graphics-opengl-glfw", "fei-graphics", "fei-rendering", "fei-imgui", "fei-pbr", "fei-scene", "fei-scripting-lua")
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
        "fei-sprite",
        "fei-graphics-vulkan",
        "fei-graphics-vulkan-glfw",
        "fei-graphics-webgpu",
        "fei-graphics-webgpu-glfw"
    )
