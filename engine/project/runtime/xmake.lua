target("entisium-project-runtime")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("entisium-app", "entisium-project")

if not is_plat("wasm") then
    target("entisium-project-runtime-tests")
        set_kind("binary")
        set_default(false)
        add_rules("entisium.test", "entisium.reflect")
        add_files("tests/*.cpp")
        add_deps("entisium-project-runtime")
end
