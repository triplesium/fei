target("fei-project-runtime")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("fei-app", "fei-project")

if not is_plat("wasm") then
    target("fei-project-runtime-tests")
        set_kind("binary")
        set_default(false)
        add_rules("fei.test", "fei.reflect")
        add_files("tests/*.cpp")
        add_deps("fei-project-runtime", "fei-scripting-lua")
end
