target("entisium-luau-defgen")
    set_kind("binary")
    set_default(false)
    set_policy("build.fence", true)
    add_files("*.cpp")
    add_headerfiles("*.hpp")
    add_packages("cli11", "nlohmann_json")

target("entisium-luau-defgen-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("emitter.cpp", "manifest.cpp", "model.cpp", "type_mapper.cpp", "tests/*.cpp")
    add_includedirs(".")
    add_packages("nlohmann_json")

rule("entisium.luau-definitions")
    on_load(function(target)
        import("luau_defgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).configure_target(target)
    end)

    after_build(function(target)
        import("luau_defgen.rules", {
            rootdir = path.join(os.projectdir(), "tools")
        }).generate(target)
    end)
rule_end()
