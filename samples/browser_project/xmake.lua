target("sample-browser-project")
    set_kind("binary")
    add_rules("entisium.reflect")
    add_asset_bundle("web-project", path.join(os.scriptdir(), "project"))
    add_files(path.join(os.projectdir(), "runtime/browser/src/main.cpp"))
    add_deps(
        "entisium-core",
        "entisium-editor-runtime-clock",
        "entisium-project-runtime",
        "entisium-project-scripting-luau",
        "entisium-runtime-inspection",
        "entisium-runtime-inspection-playtest",
        "entisium-runtime-inspection-profiling",
        "entisium-sprite",
        "entisium-ui-rendering",
        "entisium-window-browser",
        "entisium-graphics-webgpu-browser"
    )
    add_browser_shell(path.join(os.projectdir(), "runtime/browser/shell.html"))
