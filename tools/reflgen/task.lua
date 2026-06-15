function run()
    import("reflgen.rules", {
        rootdir = path.join(os.projectdir(), "tools")
    }).generate_all()
end
