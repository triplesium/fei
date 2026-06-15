target("fei-web-preview")
    set_kind("static")
    add_headerfiles("**.hpp")
    add_files("*.cpp")
    add_deps(
        "fei-app",
        "fei-base",
        "fei-ecs",
        "fei-graphics",
        "fei-graphics-opengl",
        "fei-pbr"
    )
    add_packages("cpp-httplib", "glad", "stb")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-web-preview-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-web-preview")
