target("fei-project-scripting-luau")
    set_kind("static")
    add_rules("fei.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/plugin.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-app",
        "fei-asset",
        "fei-project",
        "fei-project-scripting",
        "fei-scripting-luau"
    )
    add_packages("luau")
    if not is_plat("wasm") then
        add_files("src/playtest.cpp")
        add_deps("fei-runtime-protocol")
        add_packages("nlohmann_json")
    end

if not is_plat("wasm") then
    target("fei-project-scripting-luau-tests")
        set_kind("binary")
        set_default(false)
        add_rules("fei.test", "fei.reflect")
        add_files("tests/*.cpp")
        add_deps(
            "fei-project-runtime",
            "fei-project-scripting-lua",
            "fei-project-scripting-luau"
        )
        add_packages("nlohmann_json")
end
