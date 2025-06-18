target("sample-helloworld")
    set_kind("binary")
    add_files("helloworld/*.cpp")
    add_deps("fei-refl", "fei-ecs")
