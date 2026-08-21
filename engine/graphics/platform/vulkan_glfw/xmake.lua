target("fei-graphics-vulkan-glfw")
    set_kind("static")
    add_rules("fei.reflect")
    set_default(false)
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-graphics",
        "fei-graphics-vulkan",
        "fei-shader-vulkan",
        "fei-window-glfw",
        "fei-profiling"
    )
    add_packages("glfw", "vulkansdk")

target("fei-graphics-vulkan-glfw-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-graphics-vulkan-glfw")
