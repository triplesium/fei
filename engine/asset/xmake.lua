target("entisium-asset")
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
        "entisium-task",
        "entisium-serialization"
    )
    add_packages("yaml-cpp")

target("entisium-asset-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_deps("entisium-asset")
