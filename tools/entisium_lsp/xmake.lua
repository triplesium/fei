if not is_plat("wasm") then
target("entisium-lsp")
    set_kind("binary")
    set_default(false)
    add_files("src/*.cpp")
    add_includedirs("../../engine/scripting/include")
    add_packages("luau-lsp")

if has_config("tests") then
    target("entisium-lsp-tests")
        set_kind("binary")
        set_default(false)
        add_rules("entisium.test")
        add_files(
            "tests/*.cpp",
            "src/completion_detail.cpp",
            "src/definition_index.cpp",
            "src/diagnostics.cpp",
            "src/hover_signature.cpp",
            "src/public_type_names.cpp",
            "src/script_type_registry.cpp",
            "src/script_type_values.cpp"
        )
        add_includedirs("src", "../../engine/scripting/include")
        add_packages("luau-lsp")
        add_defines(
            "ETS_PROJECT_ROOT=\"" .. os.projectdir():gsub("\\", "/") .. "\""
        )
end
end
