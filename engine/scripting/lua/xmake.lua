target("entisium-scripting-lua")
    set_kind("static")
    add_rules("entisium.reflect")
    add_files("src/*.cpp", "src/detail/*.cpp")
    add_headerfiles("include/**.hpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-base",
        "entisium-refl",
        "entisium-ecs",
        "entisium-app",
        "entisium-asset",
        "entisium-scripting"
    )
    add_packages("lua")

target("entisium-scripting-lua-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_headerfiles("tests/*.hpp")
    add_deps("entisium-scripting-lua", "entisium-core", "entisium-math")
    add_packages("lua")
