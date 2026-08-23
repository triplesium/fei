target("entisium-ui-rendering")
    set_kind("static")
    add_rules("entisium.reflect")
    add_shader_source("ui", path.join(os.scriptdir(), "shaders"))
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
        "entisium-graphics",
        "entisium-rendering",
        "entisium-text",
        "entisium-ui"
    )

target("entisium-ui-rendering-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test", "entisium.reflect")
    add_files("tests/*.cpp")
    add_deps("entisium-ui-rendering")
