target("fei-agentd-core")
    set_kind("static")
    add_headerfiles(
        "src/artifact_store.hpp",
        "src/process.hpp",
        "src/project_descriptor.hpp",
        "src/server.hpp",
        "src/state.hpp"
    )
    add_files(
        "src/state.cpp",
        "src/server.cpp",
        "src/artifact_store.cpp",
        "src/process.cpp",
        "src/project_descriptor.cpp"
    )
    add_includedirs("src", {public = true})
    add_deps("fei-base", "fei-project", "fei-runtime-protocol")
    add_packages("cpp-httplib", "nlohmann_json")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-agentd")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("fei-agentd-core")

target("fei-ctl")
    set_kind("binary")
    add_files("src/ctl.cpp")
    add_packages("cpp-httplib", "nlohmann_json")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-agentd-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-agentd-core")
    add_packages("nlohmann_json")
