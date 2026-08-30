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
local editor_root = path.join(os.projectdir(), "editor")

target("entisium-editor-dependencies")
    set_kind("phony")
    set_default(false)
    add_extrafiles(
        path.join(editor_root, "package.json"),
        path.join(editor_root, "package-lock.json")
    )
    on_build(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "runtime"),
        }).install(target)
    end)

target("entisium-editor-runtime")
    set_kind("binary")
    set_policy("build.fence", true)
    add_rules("entisium.reflect", "entisium.luau-definitions")
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
    add_deps("entisium-editor-dependencies", "entisium-editor-runtime")
    add_extrafiles(
        path.join(os.projectdir(), "editor/index.html"),
        path.join(os.projectdir(), "editor/package.json"),
        path.join(os.projectdir(), "editor/package-lock.json"),
        path.join(os.projectdir(), "editor/tsconfig*.json"),
        path.join(os.projectdir(), "editor/vite.config.ts"),
        path.join(os.projectdir(), "editor/public/**"),
        path.join(os.projectdir(), "editor/host/**"),
        path.join(os.projectdir(), "editor/src/**")
    )
    on_build(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "runtime"),
        }).build(target, {
            platform = "host",
            host = true,
        })
    end)
    on_run(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "runtime"),
        }).run(target)
    end)

target("entisium-editor-dev")
    set_kind("phony")
    set_default(false)
    add_deps("entisium-editor-dependencies", "entisium-editor-runtime")
    on_run(function(target)
        local option = import("core.base.option")
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "runtime"),
        }).dev(target, option.get("arguments"))
    end)

target("entisium-editor-demo")
    set_kind("phony")
    set_default(false)
    add_deps(
        "entisium-editor-dependencies",
        "entisium-editor-runtime",
        "entisium-lsp-wasm"
    )
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
        path.join(os.projectdir(), "editor/public/**"),
        path.join(os.projectdir(), "editor/tools/bundle-demo-project.mjs"),
        path.join(
            path.absolute(get_config("editor_demo_project"), os.projectdir()),
            "**"
        ),
        path.join(os.projectdir(), "editor/src/**")
    )
    on_build(function(target)
        local find_tool = import("lib.detect.find_tool")
        local node = assert(
            find_tool("node"),
            "Node.js is required to bundle the Web Editor demo project"
        )
        local runtime = assert(target:dep("entisium-editor-runtime"))
        local output_root = runtime:targetdir()
        local project_directory = table.wrap(
            target:values("entisium.editor_demo_project")
        )[1]
        local project_files = os.files(path.join(project_directory, "**"))
        local editor_output = import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "runtime"),
        }).build(target, {
            platform = "demo",
            files = project_files,
            values = {"demo", project_directory},
        })
        os.vrunv(
            node.program,
            {
                path.join(editor_root, "tools", "bundle-demo-project.mjs"),
                project_directory,
                path.join(editor_output, "demo-project"),
            },
            {curdir = os.projectdir()}
        )
        os.cp(
            path.join(output_root, "runtime"),
            path.join(editor_output, "runtime")
        )
        local lsp = assert(target:dep("entisium-lsp-wasm"))
        local lsp_output = path.join(editor_output, "lsp")
        os.rm(lsp_output)
        os.mkdir(lsp_output)
        local lsp_artifacts = os.files(
            path.join(lsp:targetdir(), "entisium-lsp*")
        )
        assert(#lsp_artifacts > 0, "WebAssembly Luau LSP artifacts are missing")
        for _, artifact in ipairs(lsp_artifacts) do
            os.cp(artifact, lsp_output)
        end
    end)
end
