local integration_tests = os.files("*.cpp")

if #integration_tests > 0 then
    target("tests")
        set_kind("binary")
        add_rules("fei.test")
        add_files(integration_tests)
        add_deps(
            "fei-base",
            "fei-refl",
            "fei-ecs",
            "fei-app",
            "fei-asset",
            "fei-core",
            "fei-math"
        )
end
