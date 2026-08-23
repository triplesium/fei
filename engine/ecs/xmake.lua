target("entisium-ecs")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp", "src/dynamic/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("entisium-base", "entisium-refl", "entisium-profiling")
    if is_plat("windows") then
        add_syslinks("dbghelp")
    end

target("entisium-ecs-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_deps("entisium-ecs")
