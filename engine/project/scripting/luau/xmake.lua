target("entisium-project-scripting-luau")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/plugin.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-app",
        "entisium-asset",
        "entisium-project",
        "entisium-project-scripting",
        "entisium-scripting-luau"
    )
    add_packages("luau")
    if not is_plat("wasm") then
        add_files("src/playtest.cpp")
        add_deps("entisium-runtime-protocol")
        add_packages("nlohmann_json")
    end

if not is_plat("wasm") then
    target("entisium-project-scripting-luau-tests")
        set_kind("binary")
        set_default(false)
        add_rules("entisium.test", "entisium.reflect")
        add_files("tests/*.cpp")
        add_deps(
            "entisium-core",
            "entisium-project-runtime",
            "entisium-project-scripting-lua",
            "entisium-project-scripting-luau"
        )
        add_packages("nlohmann_json")
end
