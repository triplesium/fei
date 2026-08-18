target("fei-agentd-core")
    set_kind("static")
    add_headerfiles(
        "src/artifact_store.hpp",
        "src/process.hpp",
        "src/play_trace_store.hpp",
        "src/project_descriptor.hpp",
        "src/server.hpp",
        "src/state.hpp",
        "src/ui_assets.hpp"
    )
    add_files(
        "src/state.cpp",
        "src/server.cpp",
        "src/artifact_store.cpp",
        "src/process.cpp",
        "src/play_trace_store.cpp",
        "src/project_descriptor.cpp",
        "src/ui_assets.cpp"
    )
    add_rules(
        "utils.bin2obj",
        {
            extensions = {".html", ".css", ".js"},
            symbol_prefix = "_binary_agentd_"
        }
    )
    add_files("ui/index.html", "ui/app.css", "ui/app.js")
    add_includedirs("src", {public = true})
    add_deps("fei-asset", "fei-base", "fei-project", "fei-runtime-protocol")
    add_packages("cpp-httplib", "nlohmann_json")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-agentd")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("fei-agentd-core")

target("fei-play-runner")
    set_kind("static")
    add_headerfiles("src/play_runner.hpp")
    add_files("src/play_runner.cpp")
    add_includedirs("src", {public = true})
    add_deps("fei-base")
    add_packages("luau", "nlohmann_json")

target("fei-ctl")
    set_kind("binary")
    add_files("src/ctl.cpp")
    add_deps("fei-play-runner")
    add_packages("cpp-httplib", "nlohmann_json")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-agentd-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-agentd-core", "fei-play-runner")
    add_packages("nlohmann_json")
