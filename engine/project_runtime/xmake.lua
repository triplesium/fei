target("fei-project-runtime")
    set_kind("static")
    add_rules("fei.reflect")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps(
        "fei-base",
        "fei-app",
        "fei-asset",
        "fei-project",
        "fei-serialization",
        "fei-scene",
        "fei-core"
    )

target("fei-project-runtime-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test", "fei.reflect")
    add_files("tests/*.cpp")
    add_deps("fei-project-runtime", "fei-scripting-lua")
