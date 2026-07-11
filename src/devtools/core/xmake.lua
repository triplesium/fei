target("fei-devtools")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-app",
        "fei-asset",
        "fei-base",
        "fei-ecs",
        "fei-serialization"
    )
    add_packages("cpp-httplib", "nlohmann_json")
    add_rules("utils.bin2obj", {extensions = {".html"}})
    add_files("ui/index.html", {zeroend = true})
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("fei-devtools-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_deps("fei-devtools")
