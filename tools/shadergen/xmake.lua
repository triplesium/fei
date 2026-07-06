add_requires("cli11", "spirv-cross", "nlohmann_json")

target("fei-shadergen")
    set_kind("binary")
    set_default(false)
    set_policy("build.fence", true)
    add_files("*.cpp")
    add_packages("spirv-cross", "cli11", "nlohmann_json")

rule("fei.shader")
    set_extensions(".vert", ".geom", ".frag", ".comp", ".slang")

    on_load(function(target)
        target:add("deps", "fei-shadergen", {links = false})
    end)

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("shadergen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).buildcmd_file(target, batchcmds, sourcefile, opt)
    end)
rule_end()
