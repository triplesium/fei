function add_sample(name)
    target("sample-" .. name)
        set_kind("binary")
        add_files(name .. ".cpp")
        add_deps("fei-refl", "fei-ecs", "fei-app", "fei-window", "fei-graphics", "fei-graphics-opengl")
end

add_sample("helloworld")
add_sample("app")
add_sample("sprite")
