if not is_plat("wasm") then
    add_requires("wgpu-native v27.0.4+0")
end

target("fei-graphics-webgpu")
    set_kind("static")
    set_default(false)
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("fei-graphics", "fei-profiling")
    if is_plat("wasm") then
        add_cxflags("--use-port=emdawnwebgpu", {force = true, public = true})
        add_ldflags(
            "--use-port=emdawnwebgpu",
            "-sJSPI",
            {force = true, public = true}
        )
    else
        add_packages("wgpu-native", {public = true})
    end

target("fei-graphics-webgpu-tests")
    set_kind("binary")
    set_default(false)
    add_rules("fei.test")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("fei-graphics-webgpu")
