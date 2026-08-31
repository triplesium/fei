import("core.project.depend")

local editor_root = path.join(os.projectdir(), "editor")

local function editor_program(name)
    local extension = os.host() == "windows" and ".cmd" or ""
    local program = path.join(
        editor_root,
        "node_modules",
        ".bin",
        name .. extension
    )
    assert(os.isfile(program), "missing Editor build tool: " .. program)
    return program
end

local function editor_files(patterns)
    local files = {}
    for _, pattern in ipairs(patterns) do
        table.join2(files, os.files(path.join(editor_root, pattern)))
    end
    table.sort(files)
    return files
end

function install(target)
    local node_modules = path.join(editor_root, "node_modules")
    local extension = os.host() == "windows" and ".cmd" or ""
    local tools_available = os.isfile(
        path.join(node_modules, ".bin", "tsc" .. extension)
    ) and os.isfile(
        path.join(node_modules, ".bin", "vite" .. extension)
    )
    local package_lock = path.join(editor_root, "package-lock.json")
    local install_marker = path.join(
        node_modules,
        ".entisium-package-lock"
    )
    local package_lock_digest = hash.sha256(package_lock)
    local installed_digest = os.isfile(install_marker) and
        io.readfile(install_marker):trim() or nil
    local install_required = not tools_available or
        installed_digest ~= package_lock_digest
    local dependency_file = target:dependfile("editor-package-lock")
    local adopt_existing = tools_available and not installed_digest

    depend.on_changed(function()
        if not adopt_existing then
            local find_tool = import("lib.detect.find_tool")
            local npm_name = os.host() == "windows" and "npm.cmd" or "npm"
            local npm = assert(
                find_tool(npm_name),
                "npm is required to build the Web Editor"
            )
            os.vrunv(npm.program, {"ci"}, {curdir = editor_root})
        end
        io.writefile(install_marker, package_lock_digest .. "\n")
    end, {
        changed = install_required,
        dependfile = dependency_file,
        files = {
            package_lock,
        },
    })
end

function build(target, options)
    local platform = assert(options.platform)
    local source_files = editor_files({
        "src/**",
        "index.html",
        "package.json",
        "package-lock.json",
        "public/**",
        "tsconfig.json",
        "vite.config.ts",
    })
    if options.host then
        table.join2(
            source_files,
            editor_files({"host/**", "tsconfig.host.json"})
        )
    end
    table.join2(source_files, options.files or {})
    table.sort(source_files)

    local runtime = assert(target:dep("entisium-editor-runtime"))
    local output_name = platform == "demo" and "editor-demo" or "editor"
    local editor_output = path.join(runtime:targetdir(), output_name)
    local renderer_output = path.join(editor_root, "dist", platform)
    local expected_outputs = {path.join(editor_output, "index.html")}
    if options.host then
        table.insert(
            expected_outputs,
            path.join(editor_root, "host-dist", "main.js")
        )
    end

    local missing_output = false
    for _, output in ipairs(expected_outputs) do
        if not os.isfile(output) then
            missing_output = true
            break
        end
    end

    depend.on_changed(function()
        os.vrunv(editor_program("tsc"), {"-b"}, {curdir = editor_root})
        os.vrunv(
            editor_program("vite"),
            {"build", "--mode", platform},
            {curdir = editor_root}
        )
        if options.host then
            os.vrunv(
                editor_program("tsc"),
                {"-p", "tsconfig.host.json"},
                {curdir = editor_root}
            )
        end

        os.rm(editor_output)
        os.mkdir(editor_output)
        os.cp(path.join(renderer_output, "*"), editor_output)
    end, {
        changed = missing_output,
        dependfile = target:dependfile("editor-" .. platform),
        files = source_files,
        values = options.values or {platform},
    })

    return editor_output
end

local function project_argument(value)
    assert(value and #value > 0, "--project requires a directory path")
    local directory = path.absolute(value, os.projectdir())
    assert(
        os.isdir(directory),
        "Editor project directory does not exist: " .. directory
    )
    assert(
        os.isfile(path.join(directory, "project.yaml")),
        "Editor project directory has no project.yaml: " .. directory
    )
    return directory
end

local function editor_arguments(arguments)
    local result = {}
    local index = 1
    while index <= #arguments do
        local argument = arguments[index]
        if argument == "--" then
            -- Xmake may preserve the option separator in target arguments.
        elseif argument == "--project" then
            index = index + 1
            table.insert(result, "--project")
            table.insert(result, project_argument(arguments[index]))
        elseif argument:startswith("--project=") then
            local value = argument:sub(#"--project=" + 1)
            table.insert(result, "--project=" .. project_argument(value))
        else
            table.insert(result, argument)
        end
        index = index + 1
    end
    return result
end

function run(target)
    local option = import("core.base.option")
    local find_tool = import("lib.detect.find_tool")
    local node = assert(
        find_tool("node"),
        "Node.js is required to run the Web Editor"
    )
    local runtime = assert(target:dep("entisium-editor-runtime"))
    local runtime_directory = path.absolute(runtime:targetdir(), os.projectdir())
    local host = path.join(editor_root, "host-dist", "main.js")
    assert(os.isfile(host), "Web Editor Host has not been built: " .. host)
    assert(
        os.isfile(path.join(runtime_directory, "runtime", "index.html")),
        "WebAssembly Editor runtime has not been built: " .. runtime_directory
    )

    local arguments = {host}
    table.join2(
        arguments,
        editor_arguments(table.wrap(option.get("arguments")))
    )
    print("Web Editor runtime: %s", runtime_directory)
    os.execv(node.program, arguments, {
        curdir = editor_root,
        setenvs = {ETS_EDITOR_RUNTIME_DIR = runtime_directory},
    })
end

function run_demo(target)
    local option = import("core.base.option")
    local runtime = assert(target:dep("entisium-editor-runtime"))
    local runtime_directory = path.absolute(runtime:targetdir(), os.projectdir())
    local demo = path.join(runtime_directory, "editor-demo", "index.html")
    assert(os.isfile(demo), "Web Editor demo has not been built: " .. demo)

    local arguments = {
        "preview",
        "--outDir",
        runtime_directory,
        "--host",
        "127.0.0.1",
    }
    for _, argument in ipairs(table.wrap(option.get("arguments"))) do
        if argument ~= "--" then
            table.insert(arguments, argument)
        end
    end

    print("Web Editor demo entry: /editor-demo/index.html")
    os.execv(editor_program("vite"), arguments, {curdir = editor_root})
end

function dev(target, editor_options)
    local find_tool = import("lib.detect.find_tool")
    local npm_name = os.host() == "windows" and "npm.cmd" or "npm"
    local npm = assert(
        find_tool(npm_name),
        "npm is required to develop the Web Editor"
    )
    local runtime = assert(target:dep("entisium-editor-runtime"))
    local runtime_directory = path.absolute(runtime:targetdir(), os.projectdir())
    assert(
        os.isfile(path.join(runtime_directory, "runtime", "index.html")),
        "WebAssembly Editor runtime has not been built: " .. runtime_directory
    )

    local editor_args = editor_arguments(table.wrap(editor_options))
    local arguments = {"run", "dev"}
    if #editor_args > 0 then
        table.insert(arguments, "--")
        table.join2(arguments, editor_args)
    end
    print("Web Editor runtime: %s", runtime_directory)
    os.execv(npm.program, arguments, {
        curdir = editor_root,
        setenvs = {ETS_EDITOR_RUNTIME_DIR = runtime_directory},
    })
end
