local slang_version = "2026.14.1"

package("entisium-slang-wasm")
    set_kind("library")
    set_homepage("https://github.com/shader-slang/slang")
    set_description("Prebuilt Slang shader compiler libraries for WebAssembly")
    set_license("Apache-2.0")
    add_urls(
        "https://github.com/shader-slang/slang/releases/download/v$(version)/slang-$(version)-wasm-libs.zip"
    )
    add_versions(
        slang_version,
        "c6f1942f83324bb951c7d24aa30a5c32125206b182b2cf04508cc2ac4d42febd"
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
        os.cp("include/**.h", package:installdir("include"))
        os.cp("lib/**.a", package:installdir("lib"))
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
