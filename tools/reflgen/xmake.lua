local function find_llvm_root(package, find_tool)
    local function has_libclang(root)
        return root and os.isfile(path.join(root, "include", "clang-c", "Index.h"))
    end

    local root = os.getenv("LLVM_ROOT")
    if has_libclang(root) then
        return root
    end

    local llvm = package:dep("llvm")
    root = llvm and llvm:installdir()
    if has_libclang(root) then
        return root
    end

    local clang = find_tool("clang")
    root = clang and path.directory(path.directory(clang.program))
    if has_libclang(root) then
        return root
    end
end

package("llvm-libclang")
    set_kind("library")
    add_deps("llvm >=20.1.8 <22.0.0")
    on_fetch(function(package)
        local root = find_llvm_root(package, import("lib.detect.find_tool"))
        if root then
            return {
                includedirs = path.join(root, "include"),
                linkdirs = path.join(root, "lib"),
                links = package:is_plat("windows") and "libclang" or "clang",
                version = package:dep("llvm"):version()
            }
        end
    end)
    on_load(function(package)
        local root = find_llvm_root(package, import("lib.detect.find_tool"))
        if root then
            package:addenv("PATH", path.join(root, "bin"))
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
