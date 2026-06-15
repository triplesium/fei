add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

add_requires("catch2", "stb", "glad", "lua", "tinyobjloader", "mikktspace")
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

rule("fei.test")
    on_load(function(target)
        target:add("packages", "catch2")
        target:add("tests", "default")
        target:add("deps", "fei-test-support")
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
