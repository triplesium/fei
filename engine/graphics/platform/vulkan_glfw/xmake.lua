target("entisium-graphics-vulkan-glfw")
    set_kind("static")
    add_rules("entisium.reflect")
    set_default(false)
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-graphics",
        "entisium-graphics-vulkan",
        "entisium-shader-vulkan",
        "entisium-window-glfw",
        "entisium-profiling"
    )
    add_packages("glfw", "vulkansdk")

target("entisium-graphics-vulkan-glfw-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_deps("entisium-graphics-vulkan-glfw")
