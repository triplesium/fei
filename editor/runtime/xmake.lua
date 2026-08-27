target("entisium-editor-runtime-clock")
    set_kind("static")
    add_headerfiles("project_clock.hpp")
    add_files("project_clock.cpp")
    add_includedirs(".", {public = true})
    add_deps("entisium-core")

if not is_plat("wasm") then
target("entisium-editor-runtime-clock-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_deps("entisium-editor-runtime-clock")
end

if is_plat("wasm") then
target("entisium-editor-runtime")
    set_kind("binary")
    add_rules("entisium.reflect")
    add_files("main.cpp")
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
    add_browser_shell(path.join(os.scriptdir(), "shell.html"))
    after_build(function(target)
        local runtime_output = path.join(target:targetdir(), "runtime")
        os.rm(runtime_output)
        os.mkdir(runtime_output)
        os.cp(target:targetfile(), path.join(runtime_output, "index.html"))
        for _, extension in ipairs({".js", ".wasm", ".data"}) do
            local artifact = path.join(target:targetdir(), target:name() .. extension)
            if os.isfile(artifact) then
                os.cp(artifact, runtime_output)
            end
        end
    end)

target("entisium-editor")
    set_kind("phony")
    set_default(false)
    add_deps("entisium-editor-runtime")
    add_extrafiles(
        path.join(os.projectdir(), "editor/index.html"),
        path.join(os.projectdir(), "editor/package.json"),
        path.join(os.projectdir(), "editor/package-lock.json"),
        path.join(os.projectdir(), "editor/tsconfig*.json"),
        path.join(os.projectdir(), "editor/vite.config.ts"),
        path.join(os.projectdir(), "editor/host/**"),
        path.join(os.projectdir(), "editor/src/**")
    )
    on_build(function(target)
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

        local runtime = assert(target:dep("entisium-editor-runtime"))
        local output_root = runtime:targetdir()
        local editor_output = path.join(output_root, "editor")
        os.rm(editor_output)
        os.mkdir(editor_output)
        os.cp(path.join(editor_root, "dist", "host", "*"), editor_output)
    end)

target("entisium-editor-demo")
    set_kind("phony")
    set_default(false)
    add_deps("entisium-editor-runtime")
    add_values(
        "entisium.editor_demo_project",
        path.absolute(get_config("editor_demo_project"), os.projectdir())
    )
    add_extrafiles(
        path.join(os.projectdir(), "editor/index.html"),
        path.join(os.projectdir(), "editor/package.json"),
        path.join(os.projectdir(), "editor/package-lock.json"),
        path.join(os.projectdir(), "editor/tsconfig.json"),
        path.join(os.projectdir(), "editor/vite.config.ts"),
        path.join(os.projectdir(), "editor/tools/bundle-demo-project.mjs"),
        path.join(
            path.absolute(get_config("editor_demo_project"), os.projectdir()),
            "**"
        ),
        path.join(os.projectdir(), "editor/src/**")
    )
    on_build(function(target)
        local find_tool = import("lib.detect.find_tool")
        local editor_root = path.join(os.projectdir(), "editor")
        local npm_name = is_host("windows") and "npm.cmd" or "npm"
        local npm = assert(
            find_tool(npm_name),
            "npm is required to build the Web Editor"
        )
        local node = assert(
            find_tool("node"),
            "Node.js is required to bundle the Web Editor demo project"
        )
        if not os.isdir(path.join(editor_root, "node_modules")) then
            os.vrunv(npm.program, {"ci"}, {curdir = editor_root})
        end
        os.vrunv(npm.program, {"run", "build:demo"}, {curdir = editor_root})

        local runtime = assert(target:dep("entisium-editor-runtime"))
        local output_root = runtime:targetdir()
        local editor_output = path.join(output_root, "editor-demo")
        os.rm(editor_output)
        os.mkdir(editor_output)
        os.cp(path.join(editor_root, "dist", "demo", "*"), editor_output)
        os.vrunv(
            node.program,
            {
                path.join(editor_root, "tools", "bundle-demo-project.mjs"),
                table.wrap(target:values("entisium.editor_demo_project"))[1],
                path.join(editor_output, "demo-project"),
            },
            {curdir = os.projectdir()}
        )
        os.cp(
            path.join(output_root, "runtime"),
            path.join(editor_output, "runtime")
        )
    end)
end
