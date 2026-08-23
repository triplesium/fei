local slang_version = "2026.14.1"
local slang_commit = "7c58a326b1f3812411a204b19cb01e323d8f6010"

local function add_slang_source()
    set_homepage("https://github.com/shader-slang/slang")
    set_description("Slang shader compiler built from source")
    set_license("Apache-2.0")
    add_urls(
        "https://github.com/shader-slang/slang.git",
        {submodules = true}
    )
    add_versions(slang_version, slang_commit)
    add_deps("cmake >=3.25", "ninja")
    set_policy("package.cmake_generator.ninja", true)
end

local function disabled_slang_features()
    return {
        "-DSLANG_ENABLE_AFTERMATH=OFF",
        "-DSLANG_ENABLE_CUDA=OFF",
        "-DSLANG_ENABLE_DXIL=OFF",
        "-DSLANG_ENABLE_EXAMPLES=OFF",
        "-DSLANG_ENABLE_GFX=OFF",
        "-DSLANG_ENABLE_MIMALLOC=OFF",
        "-DSLANG_ENABLE_OPTIX=OFF",
        "-DSLANG_ENABLE_REPLAYER=OFF",
        "-DSLANG_ENABLE_SLANGC=OFF",
        "-DSLANG_ENABLE_SLANGD=OFF",
        "-DSLANG_ENABLE_SLANGI=OFF",
        "-DSLANG_ENABLE_SLANGRT=OFF",
        "-DSLANG_ENABLE_SLANG_GLSLANG=OFF",
        "-DSLANG_ENABLE_SLANG_PROXY=OFF",
        "-DSLANG_ENABLE_SLANG_RHI=OFF",
        "-DSLANG_ENABLE_SPLIT_DEBUG_INFO=OFF",
        "-DSLANG_ENABLE_TESTS=OFF",
        "-DSLANG_EXCLUDE_DAWN=ON",
        "-DSLANG_EXCLUDE_TINT=ON",
        "-DSLANG_SLANG_LLVM_FLAVOR=DISABLE",
    }
end

package("entisium-slang-generators")
    set_kind("binary")
    add_slang_source()
    on_install(function(package)
        local configs = disabled_slang_features()
        table.insert(configs, "-DCMAKE_BUILD_TYPE=Release")
        if is_host("windows") then
            table.insert(configs, "-DCMAKE_C_FLAGS=/utf-8")
            table.insert(configs, "-DCMAKE_CXX_FLAGS=/utf-8")
        end
        table.insert(
            configs,
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" .. package:installdir("bin")
        )
        import("package.tools.cmake").build(
            package,
            configs,
            {target = "all-generators", config = "Release"}
        )
    end)
    on_test(function(package)
        local suffix = is_host("windows") and ".exe" or ""
        for _, generator in ipairs({
            "slang-bootstrap",
            "slang-capability-generator",
            "slang-embed",
            "slang-fiddle",
            "slang-lookup-generator",
            "slang-spirv-embed-generator",
        }) do
            assert(os.isfile(package:installdir("bin", generator .. suffix)))
        end
    end)
package_end()

package("entisium-slang-wasm")
    set_kind("library")
    add_slang_source()
    add_deps("emscripten 6.0.0", {host = true})
    add_deps(
        "entisium-slang-generators " .. slang_version,
        {host = true, debug = false}
    )
    add_links(
        "slang-compiler",
        "compiler-core",
        "core",
        "cmark-gfm",
        "miniz",
        "lz4"
    )
    on_install("wasm", function(package)
        local generators = package:dep("entisium-slang-generators")
        local configs = disabled_slang_features()
        table.insert(configs, "-DCMAKE_BUILD_TYPE=Release")
        table.insert(configs, "-DSLANG_LIB_TYPE=STATIC")
        table.insert(
            configs,
            "-DSLANG_GENERATORS_PATH=" .. generators:installdir("bin")
        )
        table.insert(
            configs,
            "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=" .. package:installdir("lib")
        )
        table.insert(configs, "-DCMAKE_C_FLAGS=-fwasm-exceptions -Os")
        table.insert(configs, "-DCMAKE_CXX_FLAGS=-fwasm-exceptions -Os")

        import("package.tools.cmake").build(
            package,
            configs,
            {target = "slang", config = "Release"}
        )

        os.cp("include/**.h", package:installdir("include"))
        local version_headers = os.files(
            path.join(package:builddir(), "**/slang-tag-version.h")
        )
        assert(#version_headers > 0, "generated Slang version header not found")
        os.cp(version_headers[1], package:installdir("include"))
    end)
    on_test(function(package)
        assert(package:is_plat("wasm"), "entisium-slang-wasm only supports wasm")
        assert(package:has_cxxincludes({"slang.h", "slang-com-ptr.h"}))
        for _, library in ipairs({
            "libslang-compiler.a",
            "libcompiler-core.a",
            "libcore.a",
            "libcmark-gfm.a",
            "libminiz.a",
            "liblz4.a",
        }) do
            assert(os.isfile(package:installdir("lib", library)))
        end
    end)
package_end()
