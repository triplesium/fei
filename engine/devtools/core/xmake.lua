target("entisium-devtools")
    set_kind("static")
    add_rules("entisium.reflect")
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_rules(
        "utils.bin2obj",
        {
            extensions = {".html", ".css", ".js"},
            symbol_prefix = "_binary_devtools_"
        }
    )
    add_files(
        "ui/index.html",
        "ui/app.css",
        "ui/app.js"
    )
    add_includedirs("include", {public = true})
    add_deps(
        "entisium-app",
        "entisium-asset",
        "entisium-base",
        "entisium-ecs",
        "entisium-refl",
        "entisium-serialization"
    )
    add_packages("cpp-httplib", "nlohmann_json")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    end

target("entisium-devtools-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("entisium-devtools")
    add_packages("nlohmann_json")
