add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

add_requires("catch2", "stb", "glad", "lua", "tinyobjloader", "mikktspace", "cpp-httplib", "nlohmann_json", "cli11", "spirv-cross")
add_requires("glfw", {configs = {shared = false}})
add_requires("imgui", {configs = {glfw = true, opengl3 = true}})
add_requires("llvm-libclang")

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

set_policy("check.auto_ignore_flags", false)
if is_plat("windows") then
    set_policy("run.windows_error_dialog", false)
end

local project_dir = os.scriptdir():gsub("\\", "/")
add_defines("FEI_ASSETS_PATH=\"" .. project_dir .. "/assets\"")
add_defines("FEI_SHADER_ASSETS_PATH=\"" .. project_dir .. "/build/generated/shaders\"")

rule("fei.test")
    on_load(function(target)
        target:add("packages", "catch2")
        target:add("tests", "default")
        target:add("deps", "fei-test-support")
    end)
rule_end()

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

rule("fei.shader")
    set_extensions(".vert", ".geom", ".frag", ".comp")

    on_load(function(target)
        target:add("deps", "fei-shadergen")
    end)

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        import("shadergen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).buildcmd_file(target, batchcmds, sourcefile, opt)
    end)
rule_end()

target("fei-test-support")
    set_kind("static")
    set_default(false)
    add_files("tests/support/crt_report.cpp")

add_cxxflags("cl::/Zc:preprocessor")

includes("src")
includes("samples")
includes("tests")
includes("tools")
