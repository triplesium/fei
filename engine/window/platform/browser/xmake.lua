target("entisium-window-browser")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-base",
        "entisium-refl",
        "entisium-ecs",
        "entisium-app",
        "entisium-math",
        "entisium-input",
        "entisium-window"
    )
