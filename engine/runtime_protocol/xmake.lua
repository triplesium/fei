target("entisium-playtest")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles(
        "include/runtime_protocol/playtest.hpp",
        "include/runtime_protocol/playtest_plugin.hpp",
        "include/runtime_protocol/playtest_runner.hpp"
    )
    add_files(
        "src/json_schema.cpp",
        "src/playtest.cpp",
        "src/playtest_plugin.cpp",
        "src/playtest_runner.cpp"
    )
    add_includedirs("include", {public = true})
    add_deps("entisium-app", "entisium-base", "entisium-core")
    add_packages("nlohmann_json")

if not is_plat("wasm") then
target("entisium-runtime-protocol")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles(
        "include/runtime_protocol/probe.hpp",
        "include/runtime_protocol/protocol.hpp"
    )
    add_files("src/probe.cpp", "src/protocol.cpp")
    add_includedirs("include", {public = true})
    add_deps("entisium-app", "entisium-base", "entisium-playtest")
    add_packages("cpp-httplib", "nlohmann_json")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("entisium-runtime-protocol-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.test.cpp")
    add_deps("entisium-runtime-protocol")

target("entisium-runtime-probe-fixture")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.reflect")
    add_files("tests/probe_fixture.cpp")
    add_deps("entisium-runtime-protocol", "entisium-runtime-inspection-ecs")
end
