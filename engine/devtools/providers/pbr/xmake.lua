target("entisium-devtools-pbr")
    set_kind("static")
    add_shader_source("devtools_pbr", path.join(os.scriptdir(), "shaders"))
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-devtools",
        "entisium-devtools-rendering",
        "entisium-graphics",
        "entisium-pbr",
        "entisium-rendering"
    )
    add_packages("stb")

target("entisium-devtools-pbr-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test", "entisium.reflect")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("entisium-devtools-pbr")
