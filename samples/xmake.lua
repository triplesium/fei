function add_sample(name)
    target("sample-" .. name)
        set_kind("binary")
        add_rules("fei.reflect")
        add_headerfiles("common.hpp")
        add_files(name .. ".cpp")
        add_deps("fei-refl", "fei-ecs", "fei-app", "fei-window", "fei-core", "fei-asset", "fei-graphics-opengl", "fei-graphics-opengl-glfw", "fei-graphics", "fei-rendering", "fei-ui", "fei-pbr", "fei-scene", "fei-scripting-lua")
        add_packages("glfw", "glad", "imgui", "stb")
end

add_sample("refl")
add_sample("scripting")
add_sample("scene")
target("sample-scene")
    add_deps(
        "fei-devtools",
        "fei-devtools-input",
        "fei-devtools-profiling",
        "fei-devtools-rendering",
        "fei-graphics-opengl",
        "fei-graphics-opengl-glfw",
        "fei-graphics-vulkan",
        "fei-graphics-vulkan-glfw"
    )
add_sample("compute_shader")
add_sample("schedule")
add_sample("multithreading")
add_sample("graphics")
add_sample("rendering")
add_sample("asset")
