add_rules("mode.debug", "mode.release")
set_languages("c++23")
set_warnings("all")

set_runtimes("MD")
add_requires("catch2")

set_policy("check.auto_ignore_flags", false)

includes("src")
includes("samples")
includes("tests")
