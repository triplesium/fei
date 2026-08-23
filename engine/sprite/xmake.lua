target("entisium-sprite")
    set_kind("static")
    add_rules("entisium.reflect")
    add_shader_source("sprite", path.join(os.scriptdir(), "shaders"))
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-base",
        "entisium-refl",
        "entisium-ecs",
        "entisium-app",
        "entisium-math",
        "entisium-asset",
        "entisium-core",
        "entisium-graphics",
        "entisium-rendering"
    )

target("entisium-sprite-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test", "entisium.reflect")
    add_files("tests/*.cpp")
    add_includedirs("../rendering/tests")
    add_deps("entisium-sprite", "entisium-shader-opengl")
