target("fei-devtools-rendering")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-devtools",
        "fei-graphics",
        "fei-pbr",
        "fei-rendering"
    )
    add_packages("nlohmann_json", "stb")
