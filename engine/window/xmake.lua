target("fei-window")
    set_kind("static")
    add_headerfiles("include/**.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("fei-ecs")

if is_plat("wasm") then
    includes("platform/browser")
else
    includes("platform/glfw")
end
