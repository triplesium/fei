target("sample-browser")
    set_kind("binary")
    add_asset_bundle("browser", path.join(os.scriptdir(), "assets"))
    add_asset_bundle(
        "browser-fonts",
        path.join(os.scriptdir(), "../../engine/imgui/fonts")
    )
    add_files("main.cpp")
    add_deps(
        "entisium-core",
        "entisium-input",
        "entisium-input-focus",
        "entisium-sprite",
        "entisium-text",
        "entisium-ui",
        "entisium-ui-widgets",
        "entisium-ui-rendering",
        "entisium-window-browser",
        "entisium-graphics-webgpu-browser"
    )
    add_browser_shell(path.join(os.projectdir(), "runtime/browser/shell.html"))
