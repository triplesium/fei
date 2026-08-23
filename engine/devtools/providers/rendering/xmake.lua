target("entisium-devtools-rendering")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-devtools",
        "entisium-graphics",
        "entisium-rendering"
    )
    add_packages("stb")

target("entisium-devtools-rendering-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test", "entisium.reflect")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("entisium-devtools-rendering")
