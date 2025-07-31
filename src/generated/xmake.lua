target("fei-generated")
    set_kind("static")
    add_files("reflgen.cpp")
    add_headerfiles("reflgen.hpp")
    add_deps(
        "fei-app",
        "fei-base",
        "fei-core",
        "fei-ecs",
        "fei-graphics",
        "fei-graphics-opengl",
        "fei-math",
        "fei-refl",
        "fei-render2d",
        "fei-scripting",
        "fei-window"
    )
    add_packages("glad")
    add_cxxflags("cl::/bigobj")

  