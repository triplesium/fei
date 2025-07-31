target("fei-scripting")
    set_kind("static")
    add_files("**.cpp")
    add_headerfiles("**.hpp")
    add_deps("fei-base", "fei-refl", "fei-ecs", "fei-app", "fei-math", "fei-core")
    add_packages("lua")

