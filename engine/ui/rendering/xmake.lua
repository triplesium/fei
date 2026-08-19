target("fei-ui-rendering")
    set_kind("static")
    add_rules("fei.reflect")
    add_shader_source("ui", path.join(os.scriptdir(), "shaders"))
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-refl",
        "fei-ecs",
        "fei-app",
        "fei-math",
        "fei-asset",
        "fei-graphics",
        "fei-rendering",
        "fei-text",
        "fei-ui"
    )

target("fei-ui-rendering-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test", "fei.reflect")
    add_files("tests/*.cpp")
    add_deps("fei-ui-rendering")
