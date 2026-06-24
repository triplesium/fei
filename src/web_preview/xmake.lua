target("fei-web-preview")
    set_kind("static")
    add_headerfiles("**.hpp")
    add_files("*.cpp")
    add_deps(
        "fei-app",
        "fei-asset",
        "fei-base",
        "fei-ecs",
        "fei-graphics",
        "fei-pbr",
        "fei-window"
    )
    add_packages("cpp-httplib", "stb")
    add_rules("utils.bin2obj", {extensions = {".html"}})
    add_files("index.html", {zeroend = true})
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-web-preview-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-web-preview")
