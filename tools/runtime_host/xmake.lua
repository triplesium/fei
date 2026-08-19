target("fei-runtime-host-core")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/application.cpp")
    add_rules(
        "utils.bin2obj",
        {
            extensions = {".ttf"},
            symbol_prefix = "_binary_runtime_host_"
        }
    )
    add_files("../../engine/imgui/fonts/Cousine-Regular.ttf", {zeroend = true})
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-app",
        "fei-core",
        "fei-project",
        "fei-project-runtime",
        "fei-project-scripting-lua",
        "fei-project-scripting-luau",
        "fei-window",
        "fei-rendering",
        "fei-sprite",
        "fei-input-focus",
        "fei-text",
        "fei-ui",
        "fei-ui-widgets",
        "fei-ui-rendering",
        "fei-graphics-opengl",
        "fei-graphics-opengl-glfw",
        "fei-runtime-protocol",
        "fei-runtime-inspection-ecs"
    )
    add_packages("glfw", "nlohmann_json", "stb")

target("fei-runtime-host")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_rules("fei.reflect")
    add_files("src/main.cpp")
    add_deps("fei-runtime-host-core", "fei-project")
    add_packages("glfw", "glad")
