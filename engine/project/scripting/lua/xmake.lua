target("entisium-project-scripting-lua")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-app",
        "entisium-asset",
        "entisium-project",
        "entisium-project-scripting",
        "entisium-scripting-lua"
    )

target("entisium-project-scripting-lua-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test", "entisium.reflect")
    add_files("tests/*.cpp")
    add_deps("entisium-project-runtime", "entisium-project-scripting-lua")
