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
        "entisium-sprite",
        "entisium-ui-rendering",
        "entisium-window-browser",
        "entisium-graphics-webgpu-browser"
    )
    add_browser_shell(path.join(os.scriptdir(), "shell.html"))
    add_extrafiles(
        path.join(os.projectdir(), "editor/index.html"),
        path.join(os.projectdir(), "editor/package.json"),
        path.join(os.projectdir(), "editor/package-lock.json"),
        path.join(os.projectdir(), "editor/tsconfig.json"),
        path.join(os.projectdir(), "editor/vite.config.ts"),
        path.join(os.projectdir(), "editor/host/**"),
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
        local editor_output = path.join(target:targetdir(), "editor")
        os.rm(editor_output)
        os.mkdir(editor_output)
        os.cp(path.join(editor_root, "dist", "*"), editor_output)

        local runtime_output = path.join(target:targetdir(), "runtime")
        os.rm(runtime_output)
        os.mkdir(runtime_output)
        os.cp(target:targetfile(), path.join(runtime_output, "index.html"))
        for _, extension in ipairs({".js", ".wasm", ".data"}) do
            local artifact = path.join(
                target:targetdir(),
                target:name() .. extension
            )
            if os.isfile(artifact) then
                os.cp(artifact, runtime_output)
            end
        end
    end)
end
