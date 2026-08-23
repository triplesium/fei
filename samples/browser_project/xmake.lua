target("sample-browser-project")
    set_kind("binary")
    add_rules("entisium.reflect")
    add_asset_bundle("web-project", path.join(os.scriptdir(), "project"))
    add_files("main.cpp")
    add_deps(
        "entisium-core",
        "entisium-project-runtime",
        "entisium-project-scripting-luau",
        "entisium-runtime-inspection",
        "entisium-sprite",
        "entisium-window-browser",
        "entisium-graphics-webgpu-browser"
    )
    add_browser_shell(path.join(os.scriptdir(), "../browser/shell.html"))
    add_extrafiles(
        path.join(os.projectdir(), "editor/index.html"),
        path.join(os.projectdir(), "editor/package.json"),
        path.join(os.projectdir(), "editor/package-lock.json"),
        path.join(os.projectdir(), "editor/tsconfig.json"),
        path.join(os.projectdir(), "editor/vite.config.ts"),
        path.join(os.projectdir(), "editor/src/**")
    )

    before_build(function()
        local find_tool = import("lib.detect.find_tool")
        local editor_root = path.join(os.projectdir(), "editor")
        local npm_name = is_host("windows") and "npm.cmd" or "npm"
        local npm = assert(
            find_tool(npm_name),
            "npm is required to build the Web Editor"
        )
        if not os.isdir(path.join(editor_root, "node_modules")) then
            os.vrunv(npm.program, {"ci"}, {curdir = editor_root})
        end
        os.vrunv(npm.program, {"run", "build"}, {curdir = editor_root})
    end)

    after_build(function(target)
        local editor_root = path.join(os.projectdir(), "editor")
        local output_root = path.join(target:targetdir(), "editor")
        os.rm(output_root)
        os.mkdir(output_root)
        os.cp(path.join(editor_root, "dist", "*"), output_root)
    end)
