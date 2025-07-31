function add_sample(name)
    target("sample-" .. name)
        set_kind("binary")
        add_files(name .. ".cpp")
        add_deps("fei-refl", "fei-ecs", "fei-app", "fei-window", "fei-graphics", "fei-graphics-opengl", "fei-core", "fei-render2d", "fei-scripting", "fei-generated")
end

add_sample("helloworld")
add_sample("app")
add_sample("sprite")
add_sample("refl")
add_sample("scripting")
