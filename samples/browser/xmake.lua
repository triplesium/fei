target("sample-browser")
    set_kind("binary")
    add_asset_bundle("browser", path.join(os.scriptdir(), "assets"))
    add_asset_bundle(
        "browser-fonts",
        path.join(os.scriptdir(), "../../engine/imgui/fonts")
    )
    add_files("main.cpp")
    add_deps(
        "fei-core",
        "fei-input",
        "fei-input-focus",
        "fei-sprite",
        "fei-text",
        "fei-ui",
        "fei-ui-widgets",
        "fei-ui-rendering",
        "fei-window-browser",
        "fei-graphics-webgpu-browser"
    )
    add_ldflags(
        "--shell-file",
        path.join(os.scriptdir(), "shell.html"),
        {force = true}
    )
    add_extrafiles("shell.html")
