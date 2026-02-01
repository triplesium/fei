target("fei-rendering")
    set_kind("static")
    add_headerfiles("**.hpp")
    add_files("**.cpp")
    add_deps("fei-base", "fei-refl", "fei-ecs", "fei-app", "fei-math", "fei-asset", "fei-graphics", "fei-window")
    add_packages("tinyobjloader", "glfw")

