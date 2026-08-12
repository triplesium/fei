target("fei-runtime-inspection-ecs")
    set_kind("static")
    add_rules("fei.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-runtime-inspection",
        "fei-ecs",
        "fei-refl",
        "fei-serialization"
    )

target("fei-runtime-inspection-ecs-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test", "fei.reflect")
    add_files("tests/*.cpp")
    add_deps("fei-runtime-inspection-ecs")
    add_packages("nlohmann_json")
