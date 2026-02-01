target("fei-pbr")
    set_kind("static")
    add_headerfiles("**.hpp")
    add_files("**.cpp")
    add_deps("fei-base", "fei-refl", "fei-ecs", "fei-app", "fei-math", "fei-asset", "fei-graphics", "fei-window", "fei-rendering")
    add_rules("utils.bin2obj", {extensions = {".vert", ".frag"}})
    add_files("shaders/*.vert", {zeroend = true})
    add_files("shaders/*.frag", {zeroend = true})

