target("fei-ecs")
    set_kind("static")
    add_headerfiles("**.hpp")
    add_files("*.cpp", "dynamic/*.cpp")
    add_deps("fei-base", "fei-refl", "fei-profiling")
    if is_plat("windows") then
        add_syslinks("dbghelp")
    end

target("fei-ecs-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-ecs")
