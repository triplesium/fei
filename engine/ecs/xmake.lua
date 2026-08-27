target("entisium-ecs")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp", "src/dynamic/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("entisium-base", "entisium-refl", "entisium-profiling")
    if is_plat("wasm") then
        add_ldflags(
            "--js-library",
            path.join(os.scriptdir(), "src/system_profile_wasm.js"),
            {force = true, public = true}
        )
        add_extrafiles("src/system_profile_wasm.js")
    end
    if is_plat("windows") then
        add_syslinks("dbghelp")
    end

target("entisium-ecs-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_deps("entisium-ecs")
