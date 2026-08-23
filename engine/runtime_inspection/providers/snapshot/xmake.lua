target("entisium-runtime-inspection-snapshot")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-runtime-inspection",
        "entisium-snapshot",
        "entisium-serialization"
    )

target("entisium-runtime-inspection-snapshot-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.test.cpp")
    add_deps("entisium-runtime-inspection-snapshot")
    add_packages("nlohmann_json")

target("entisium-snapshot-runtime-fixture")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.reflect")
    add_files("tests/runtime_fixture.cpp")
    add_deps(
        "entisium-project",
        "entisium-runtime-inspection-snapshot",
        "entisium-runtime-protocol"
    )
    add_packages("nlohmann_json")
