package("luau")
    set_homepage("https://www.luau.org/")
    set_description("A fast, small, safe, gradually typed embeddable scripting language derived from Lua.")
    set_license("MIT")

    add_urls("https://github.com/luau-lang/luau.git")

    add_configs("extern_c", {default = false, type = "boolean"})
    add_configs("build_web", {default = false, type = "boolean"})

    add_deps("cmake")

    on_install(function(package)
        io.replace("extern/isocline/src/completers.c", "__finddata64_t", "_finddatai64_t", {plain = true})
        if package:is_plat("wasm") then
            io.replace("CMakeLists.txt", "-fexceptions", "-fwasm-exceptions", {plain = true})
        end

        local configs = {
            "-DLUAU_BUILD_TESTS=OFF",
            "-DLUAU_BUILD_CLI=OFF",
            "-DCMAKE_POLICY_DEFAULT_CMP0057=NEW",
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"),
            "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"),
            "-DLUAU_BUILD_WEB=" .. ((package:is_plat("wasm") or package:config("build_web")) and "ON" or "OFF"),
            "-DLUAU_EXTERN_C=" .. (package:config("extern_c") and "ON" or "OFF"),
        }

        if package:is_plat("wasm") then
            import("package.tools.cmake").build(package, configs, {target = "Luau.Web", builddir = "build"})
        else
            import("package.tools.cmake").build(package, configs, {builddir = "build"})
        end

        local cmake_file = io.readfile("CMakeLists.txt")
        local links = {}
        for library_name, library_type in cmake_file:gmatch("add_library%(([%a|%.]+) (%w+)") do
            library_type = library_type:lower()
            if library_name:startswith("Luau.") and (library_type == "static" or library_type == "interface") then
                local linkname = library_name
                if library_name:endswith(".lib") then
                    linkname = library_name:sub(1, -5)
                end
                if library_type == "static" then
                    table.insert(links, linkname)
                end
                local include_dir = linkname:sub(6):gsub("%..*", "")
                os.trycp(include_dir .. "/include/*", package:installdir("include"))
            end
        end
        for index = #links, 1, -1 do
            package:add("links", links[index])
        end

        os.trycp("build/**.a", package:installdir("lib"))
        os.trycp("build/**.so", package:installdir("lib"))
        os.trycp("build/**.dylib", package:installdir("lib"))
        os.trycp("build/**.lib", package:installdir("lib"))
        os.trycp("build/**.dll", package:installdir("bin"))
    end)

    on_test(function(package)
        assert(package:has_cxxincludes("Luau/Common.h"))
    end)
package_end()
