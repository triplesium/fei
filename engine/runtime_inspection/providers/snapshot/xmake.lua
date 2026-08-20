target("fei-runtime-inspection-snapshot")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-runtime-inspection",
        "fei-snapshot",
        "fei-serialization"
    )

target("fei-runtime-inspection-snapshot-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.test.cpp")
    add_deps("fei-runtime-inspection-snapshot")
    add_packages("nlohmann_json")

target("fei-snapshot-runtime-fixture")
    set_kind("binary")
    set_default(false)
    add_rules("fei.reflect")
    add_files("tests/runtime_fixture.cpp")
    add_deps(
        "fei-project",
        "fei-runtime-inspection-snapshot",
        "fei-runtime-protocol"
    )
    add_packages("nlohmann_json")
