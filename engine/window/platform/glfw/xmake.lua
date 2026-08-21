target("fei-window-glfw")
    set_kind("static")
    add_rules("fei.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-refl",
        "fei-ecs",
        "fei-app",
        "fei-math",
        "fei-input",
        "fei-window"
    )
    add_packages("glfw")
