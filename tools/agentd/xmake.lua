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

target("fei-agentd-checkpoint-e2e-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/e2e/checkpoint.test.cpp")
    add_deps(
        "fei-agentd-core",
        "fei-ctl",
        "fei-snapshot-runtime-fixture"
    )
    add_packages("cpp-httplib", "nlohmann_json")
    after_load(function(target)
        local fixture = target:dep("fei-snapshot-runtime-fixture")
        local fixture_path = path.absolute(fixture:targetfile()):gsub("\\", "/")
        local ctl = target:dep("fei-ctl")
        local ctl_path = path.absolute(ctl:targetfile()):gsub("\\", "/")
        local project_path = path.join(
            os.projectdir(),
            "tests/fixtures/checkpoint_project/project.yaml"
        ):gsub("\\", "/")
        target:add(
            "defines",
            "FEI_SNAPSHOT_RUNTIME_FIXTURE_PATH=\"" .. fixture_path .. "\"",
            "FEI_CTL_PATH=\"" .. ctl_path .. "\"",
            "FEI_CHECKPOINT_PROJECT_PATH=\"" .. project_path .. "\""
        )
    end)
