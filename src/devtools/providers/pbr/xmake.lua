target("fei-devtools-pbr")
    set_kind("static")
    add_rules("fei.reflect")
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-devtools",
        "fei-devtools-rendering",
        "fei-graphics",
        "fei-pbr",
        "fei-rendering"
    )
    add_packages("stb")

target("fei-devtools-pbr-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test", "fei.reflect")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("fei-devtools-pbr")
