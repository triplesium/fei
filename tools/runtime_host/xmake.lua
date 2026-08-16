target("fei-runtime-host-core")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/application.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-app",
        "fei-core",
        "fei-project",
        "fei-project-runtime",
        "fei-project-scripting-lua",
        "fei-project-scripting-luau",
        "fei-rendering",
        "fei-sprite",
        "fei-graphics-opengl",
        "fei-graphics-opengl-glfw",
        "fei-runtime-protocol",
        "fei-runtime-inspection-ecs"
    )

target("fei-runtime-host")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_rules("fei.reflect")
    add_files("src/main.cpp")
    add_deps("fei-runtime-host-core", "fei-project")
    add_packages("glfw", "glad")
