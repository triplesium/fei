import("core.project.config")

function run()
    config.load()
    import("reflgen.rules", {
        rootdir = path.join(os.projectdir(), "tools")
    }).generate_all()
end
