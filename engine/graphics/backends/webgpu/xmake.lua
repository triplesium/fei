if not is_plat("wasm") then
    add_requires("wgpu-native v27.0.4+0")
end

target("entisium-graphics-webgpu")
    set_kind("static")
    set_default(false)
    add_headerfiles("include/**.hpp", "src/*.hpp")
    add_files("src/*.cpp")
    add_includedirs("include", {public = true})
    add_deps("entisium-graphics", "entisium-profiling")
    if is_plat("wasm") then
        add_cxflags("--use-port=emdawnwebgpu", {force = true, public = true})
        add_ldflags(
            "--use-port=emdawnwebgpu",
            {force = true, public = true}
        )
    else
        add_packages("wgpu-native", {public = true})
    end

target("entisium-graphics-webgpu-tests")
    set_kind("binary")
    set_default(false)
    add_rules("entisium.test")
    add_files("tests/*.cpp")
    add_includedirs("src")
    add_deps("entisium-graphics-webgpu")
