target("entisium-runtime-inspection-ecs")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-runtime-inspection",
        "entisium-ecs",
        "entisium-refl",
        "entisium-serialization"
    )

target("entisium-runtime-inspection-ecs-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test", "entisium.reflect")
    add_files("tests/*.cpp")
    add_deps("entisium-runtime-inspection-ecs")
    add_packages("nlohmann_json")
