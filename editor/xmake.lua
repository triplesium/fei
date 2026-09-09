if is_plat("wasm") then
local editor_root = path.join(os.projectdir(), "editor")

target("entisium-editor-dependencies")
    set_kind("phony")
    set_default(false)
    add_extrafiles(
        path.join(editor_root, "package.json"),
        path.join(os.projectdir(), "package-lock.json")
    )
    on_build(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "tools"),
        }).install(target)
    end)

target("entisium-editor")
    set_kind("phony")
    set_default(false)
    add_deps("entisium-editor-dependencies", "entisium-editor-runtime")
    add_extrafiles(
        path.join(os.projectdir(), "editor/index.html"),
        path.join(os.projectdir(), "editor/package.json"),
        path.join(os.projectdir(), "package-lock.json"),
        path.join(os.projectdir(), "editor/tsconfig*.json"),
        path.join(os.projectdir(), "editor/vite.config.ts"),
        path.join(os.projectdir(), "editor/public/**"),
        path.join(os.projectdir(), "editor/src/**")
    )
    on_build(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "tools"),
        }).build(target, {
            platform = "host",
            host = true,
        })
    end)
    on_run(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "tools"),
        }).run(target)
    end)

target("entisium-editor-dev")
    set_kind("phony")
    set_default(false)
    add_deps("entisium-editor-dependencies", "entisium-editor-runtime")
    on_run(function(target)
        local option = import("core.base.option")
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "tools"),
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
        path.join(os.projectdir(), "package-lock.json"),
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
            rootdir = path.join(os.projectdir(), "editor", "tools"),
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
        local runtime_output = path.join(editor_output, "runtime")
        os.rm(runtime_output)
        os.mkdir(runtime_output)
        os.cp(path.join(output_root, "runtime", "*"), runtime_output)
        local symbol_output = path.join(editor_output, "profile-symbols")
        os.rm(symbol_output)
        if has_config("profile_summary") then
            local runtime_wasm = path.join(
                output_root,
                runtime:name() .. ".wasm"
            )
            local digest = hash.sha256(runtime_wasm)
            local manifest = path.join(
                output_root,
                "profile-symbols",
                digest .. ".json"
            )
            assert(
                os.isfile(manifest),
                "missing WebAssembly profile symbol manifest: " .. manifest
            )
            os.mkdir(symbol_output)
            os.cp(manifest, symbol_output)
        end
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
    on_run(function(target)
        import("editor_build", {
            rootdir = path.join(os.projectdir(), "editor", "tools"),
        }).run_demo(target)
    end)
end
