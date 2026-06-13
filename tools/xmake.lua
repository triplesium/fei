task("reflgen")
    on_run(function ()
        import("reflgen.task", {
            rootdir = path.join(os.projectdir(), "tools")
        }).run()
    end)

    set_menu {
        usage = "xmake reflgen",
        description = "Generate reflection metadata for the project.",
    }

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
