target("fei-scripting-luau")
    set_kind("static")
    add_rules("fei.reflect")
    add_files("src/*.cpp", "src/detail/*.cpp")
    add_headerfiles("include/**.hpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-refl",
        "fei-ecs",
        "fei-app",
        "fei-asset",
        "fei-scripting"
    )
    add_packages("luau")

target("fei-scripting-luau-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-scripting-luau", "fei-scripting-lua", "fei-core", "fei-math")
    add_packages("luau", "lua")
