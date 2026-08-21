import("core.project.config")

local tooling = import("tasks.tooling", {
    rootdir = path.join(os.projectdir(), "tools")
})

function run(options)
    options = options or {}
    config.load()
    assert(
        config.get("plat") == "wasm",
        "browser-smoke requires a wasm configuration"
    )
    os.vrunv("xmake", {"build", "-y", "sample-browser"})

    local output_root = path.join(
        os.projectdir(),
        config.get("buildir") or "build",
        config.get("plat"),
        config.get("arch"),
        config.get("mode")
    )
    local arguments = {
        path.join(os.projectdir(), "tools", "browser_smoke.mjs"),
        "--root",
        output_root,
        "--timeout",
        tostring(options.timeout or 30000)
    }
    if options.browser then
        table.join2(arguments, {"--browser", path.absolute(options.browser)})
    end
    local outdata, errdata = os.iorunv(tooling.find_program("node"), arguments, {
        curdir = os.projectdir()
    })
    if outdata and #outdata:trim() > 0 then
        print(outdata:trim())
    end
    if errdata and #errdata:trim() > 0 then
        print(errdata:trim())
    end
end
