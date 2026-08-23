local integration_tests = os.files("*.cpp")

if #integration_tests > 0 then
    target("tests")
        set_kind("binary")
        add_rules("entisium.test")
        add_files(integration_tests)
        add_deps(
            "entisium-base",
            "entisium-refl",
            "entisium-ecs",
            "entisium-app",
            "entisium-asset",
            "entisium-core",
            "entisium-math"
        )
end
