target("sample-browser-project")
    set_kind("binary")
    add_rules("fei.reflect")
    add_asset_bundle("web-project", path.join(os.scriptdir(), "project"))
    add_files("main.cpp")
    add_deps(
        "fei-core",
        "fei-project-runtime",
        "fei-project-scripting-luau",
        "fei-runtime-inspection",
        "fei-sprite",
        "fei-window-browser",
        "fei-graphics-webgpu-browser"
    )
    add_browser_shell(path.join(os.scriptdir(), "../browser/shell.html"))
