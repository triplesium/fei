target("entisium-graphics-webgpu-browser")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-window-browser",
        "entisium-graphics",
        "entisium-graphics-webgpu",
        "entisium-shader-webgpu"
    )
