task("tidy")
    on_run(function ()
        local option = import("core.base.option")
        import("tasks.tidy", {
            rootdir = path.join(os.projectdir(), "tools")
        }).run({
            jobs = option.get("jobs"),
            targets = option.get("targets"),
            verbose = option.get("verbose")
        })
    end)

    set_menu {
        usage = "xmake tidy [options] [targets]",
        description = "Run clang-tidy for project sources and headers.",
        options = {
            {"j", "jobs", "kv", tostring(os.default_njob()), "Set the number of parallel clang-tidy jobs."},
            {nil, "targets", "vs", nil, "Run clang-tidy for the given targets."}
        }
    }

task("format")
    on_run(function ()
        local option = import("core.base.option")
        import("tasks.format", {
            rootdir = path.join(os.projectdir(), "tools")
        }).run({
            check = option.get("check"),
            jobs = option.get("jobs"),
            targets = option.get("targets"),
            verbose = option.get("verbose")
        })
    end)

    set_menu {
        usage = "xmake format [options] [targets]",
        description = "Run clang-format for project sources and headers.",
        options = {
            {nil, "check", "k", nil, "Check formatting without modifying files."},
            {"j", "jobs", "kv", tostring(os.default_njob()), "Set the number of parallel clang-format jobs."},
            {nil, "targets", "vs", nil, "Run clang-format for the given targets."}
        }
    }
