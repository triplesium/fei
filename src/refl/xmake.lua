target("fei-refl")
    set_kind("static")
    add_headerfiles("**.hpp")
    add_files("**.cpp")
    add_deps("fei-base")