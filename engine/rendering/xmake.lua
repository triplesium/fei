target("entisium-rendering")
    set_kind("static")
    add_rules("entisium.reflect")
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
    add_deps("entisium-base", "entisium-refl", "entisium-ecs", "entisium-app", "entisium-math", "entisium-asset", "entisium-core", "entisium-graphics", "entisium-shader", "entisium-profiling")
    add_packages("tinyobjloader", "mikktspace")

target("entisium-rendering-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_deps("entisium-rendering", "entisium-shader-opengl", "entisium-shader-vulkan")
