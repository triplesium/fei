package("llvm-libclang")
    set_kind("library")
    add_deps("llvm 21.1.0")
    on_fetch(function(package)
        local llvm = package:dep("llvm")
        if llvm then
            local installdir = llvm:installdir()
            return {
                includedirs = path.join(installdir, "include"),
                linkdirs = path.join(installdir, "lib"),
                links = package:is_plat("windows") and "libclang" or "clang",
                version = llvm:version()
            }
        end
    end)
    on_load(function(package)
        local llvm = package:dep("llvm")
        if llvm then
            package:addenv("PATH", path.join(llvm:installdir(), "bin"))
        end
    end)
package_end()

add_requires("cli11", "llvm-libclang")

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

target("fei-reflgen")
    set_kind("binary")
    set_default(false)
    set_policy("build.fence", true)
    add_files("*.cpp")
    add_headerfiles("*.hpp")
    add_packages("llvm-libclang", "cli11")

rule("fei.reflect.file")
    set_extensions(".reflgen")

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("reflgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).buildcmd_file(target, batchcmds, sourcefile, opt)
    end)
rule_end()

rule("fei.reflect.module")
    set_extensions(".reflmod")
    add_orders("fei.reflect.file", "fei.reflect.module")

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("reflgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).buildcmd_module(target, batchcmds, opt)
    end)
rule_end()

rule("fei.reflect.aggregate")
    set_extensions(".reflagg")
    add_orders("fei.reflect.module", "fei.reflect.aggregate")

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("reflgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).buildcmd_aggregate(target, batchcmds, opt)
    end)
rule_end()

rule("fei.reflect")
    add_deps(
        "fei.reflect.file",
        "fei.reflect.module",
        "fei.reflect.aggregate"
    )

    on_load(function(target)
        local reflgen = import("reflgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        })
        reflgen.configure_target(target)
    end)

    after_clean(function(target)
        import("reflgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).clean(target)
    end)
rule_end()
