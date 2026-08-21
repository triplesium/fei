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
    add_extrafiles(path.join(os.projectdir(), "editor/**"))

    after_link(function(target)
        local editor_root = path.join(os.projectdir(), "editor")
        local output_root = path.join(target:targetdir(), "editor")
        os.mkdir(output_root)
        os.cp(path.join(editor_root, "*"), output_root)
    end)
