target("fei-profiling")
    set_kind("static")
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    if has_config("profile_summary") then
        add_defines("FEI_ENABLE_PROFILE_SUMMARY", {public = true})
    end
    if has_config("tracy") then
        add_defines(
            "FEI_ENABLE_TRACY",
            "TRACY_ENABLE",
            "TRACY_ON_DEMAND",
            "TRACY_NO_CALLSTACK",
            "TRACY_NO_CODE_TRANSFER",
            "TRACY_NO_CONTEXT_SWITCH",
            "TRACY_NO_BROADCAST",
            "TRACY_NO_SAMPLING",
            "TRACY_NO_VERIFY",
            "TRACY_NO_VSYNC_CAPTURE",
            "TRACY_NO_SYSTEM_TRACING",
            "TRACY_NO_FRAME_IMAGE",
            "TRACY_NO_CRASH_HANDLER",
            {public = true}
        )
        add_packages("tracy", {public = true})
    end

target("fei-profiling-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("fei-profiling")
