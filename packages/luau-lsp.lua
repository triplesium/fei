package("luau-lsp")
    set_homepage("https://github.com/triplesium/luau-lsp")
    set_description("Language Server Protocol implementation for Luau.")
    set_license("MIT")

    add_urls(
        "https://github.com/triplesium/luau-lsp.git",
        {submodules = true}
    )
    add_versions(
        "2026.8.25-entisium.5",
        "6efea06a1b25b944ad45b703719caf6e20b928fc"
    )

    add_deps("cmake")

    on_install(function(package)
        local configs = {
            "-DCMAKE_POLICY_DEFAULT_CMP0057=NEW",
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"),
            "-DBUILD_SHARED_LIBS=OFF",
            "-DLSP_BUILD_WITH_SENTRY=OFF",
            "-DLSP_WERROR=OFF",
            "-DLUAU_BUILD_CLI=OFF",
            "-DLUAU_BUILD_TESTS=OFF",
        }
        if package:is_plat("wasm") then
            table.insert(configs, "-DCMAKE_CXX_FLAGS=-pthread -fwasm-exceptions")
        end
        import("package.tools.cmake").build(
            package,
            configs,
            {target = "Luau.LanguageServer", builddir = "build", jobs = 8}
        )

        os.cp("src/include/*", package:installdir("include"))
        for _, include_dir in ipairs({
            "extern/json/include",
            "extern/glob/include",
            "extern/argparse/include",
            "extern/toml/include",
            "extern/ryml/include",
        }) do
            os.cp(include_dir .. "/*", package:installdir("include"))
        end
        for _, include_dir in ipairs(os.dirs("luau/*/include")) do
            os.cp(include_dir .. "/*", package:installdir("include"))
        end

        os.trycp("build/**.a", package:installdir("lib"))
        os.trycp("build/**.lib", package:installdir("lib"))
        os.trycp("build/**.pdb", package:installdir("lib"))

        package:add("links", "Luau.LanguageServer")
        package:add("links", "Luau.CodeGen")
        package:add("links", "Luau.Compiler")
        package:add("links", "Luau.Bytecode")
        package:add("links", "Luau.Analysis")
        package:add("links", "Luau.Ast")
        package:add("links", "Luau.Config")
        package:add("links", "Luau.VM")
        package:add("links", "Luau.Common")
        if package:is_plat("windows") then
            package:add("syslinks", "shell32")
        end
    end)

    on_test(function(package)
        local configs = {configs = {languages = "c++17"}}
        assert(package:has_cxxincludes("LSP/LanguageServer.hpp", configs))
        assert(package:has_cxxincludes("Luau/Ast.h", configs))
    end)
package_end()
