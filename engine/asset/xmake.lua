target("fei-asset")
    set_kind("static")
    add_rules("fei.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-refl",
        "fei-ecs",
        "fei-app",
        "fei-task",
        "fei-serialization"
    )
    add_packages("yaml-cpp")

target("fei-asset-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-asset")
