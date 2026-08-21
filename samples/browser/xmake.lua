target("sample-browser")
    set_kind("binary")
    add_asset_bundle("browser", path.join(os.scriptdir(), "assets"))
    add_files("main.cpp")
    add_deps(
        "fei-core",
        "fei-input",
        "fei-sprite",
        "fei-graphics-webgpu-browser"
    )
    add_ldflags(
        "--shell-file",
        path.join(os.scriptdir(), "shell.html"),
        {force = true}
    )
    add_extrafiles("shell.html")
