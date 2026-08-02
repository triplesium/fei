target("fei-sprite")
    set_kind("static")
    add_rules("fei.reflect")
    add_shader_source("sprite", path.join(os.scriptdir(), "shaders"))
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
        "fei-core",
        "fei-graphics",
        "fei-rendering"
    )

target("fei-sprite-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test", "fei.reflect")
    add_files("tests/*.cpp")
    add_includedirs("../rendering/tests")
    add_deps("fei-sprite")
