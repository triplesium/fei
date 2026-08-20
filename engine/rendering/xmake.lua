target("fei-rendering")
    set_kind("static")
    add_rules("fei.reflect")
    add_shader_source("rendering", path.join(os.scriptdir(), "shaders"))
    add_headerfiles("include/**.hpp")
    add_files(
        "src/defaults.cpp",
        "src/plugin.cpp",
        "src/render_app.cpp",
        "src/render_frame.cpp",
        "src/render_queue.cpp",
        "src/resource_set_cache.cpp",
        "src/shader_cache.cpp",
        "src/view.cpp",
        "src/visibility.cpp",
        "src/mesh/*.cpp"
    )
    add_includedirs("include", {public = true})
    add_deps("fei-base", "fei-refl", "fei-ecs", "fei-app", "fei-math", "fei-asset", "fei-core", "fei-graphics", "fei-shader", "fei-profiling")
    add_packages("tinyobjloader", "mikktspace")

target("fei-rendering-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-rendering", "fei-shader-opengl", "fei-shader-vulkan")
