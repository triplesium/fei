target("entisium-scripting")
    set_kind("static")
    add_rules("entisium.reflect")
    add_files("src/*.cpp", "src/compiler/*.cpp", "src/detail/*.cpp")
    add_headerfiles("include/**.hpp")
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-base",
        "entisium-refl",
        "entisium-ecs",
        "entisium-app",
        "entisium-asset"
    )
    add_packages("luau", "nlohmann_json")

if not is_plat("wasm") then
    target("entisium-scripting-tests")
        set_kind("binary")
        set_default(false)
        add_rules("entisium.test")
        add_files("tests/*.cpp")
        add_includedirs("src")
        add_deps("entisium-scripting", "entisium-core", "entisium-math")
        add_packages("luau")

    target("entisium-scripting-query-benchmark")
        set_kind("binary")
        set_default(false)
        add_files("benchmarks/query_benchmark.cpp")
        add_deps("entisium-scripting")
        add_packages("luau")
end
