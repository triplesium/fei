toolchain("fei-emcc")
    set_homepage("https://emscripten.org/")
    set_description("Emscripten 6 toolchain compatibility for Xmake 3.0")
    set_kind("standalone")

    local suffix = is_host("windows") and ".exe" or ""
    set_toolset("cc", "emcc" .. suffix)
    set_toolset("cxx", "em++" .. suffix, "emcc" .. suffix)
    set_toolset("ld", "em++" .. suffix, "emcc" .. suffix)
    set_toolset("sh", "em++" .. suffix, "emcc" .. suffix)
    set_toolset("ar", "emar" .. suffix)
    set_toolset("as", "emcc" .. suffix)
    set_toolset("ranlib", "emranlib" .. suffix)

    on_check(function(toolchain)
        local find_emsdk = import("detect.sdks.find_emsdk")
        for _, package in ipairs(toolchain:packages()) do
            local emsdk = find_emsdk(package:installdir())
            if emsdk then
                toolchain:config_set("bindir", emsdk.emscripten)
                toolchain:config_set("sdkdir", emsdk.sdkdir)
                return emsdk
            end
        end
        return find_emsdk()
    end)

    on_load(function(toolchain)
        if toolchain:is_arch("wasm64") then
            toolchain:add("cxflags", "-sMEMORY64=1")
            toolchain:add("asflags", "-sMEMORY64=1")
            toolchain:add("ldflags", "-sMEMORY64=1")
            toolchain:add("shflags", "-sMEMORY64=1")
        end

        for _, package in ipairs(toolchain:packages()) do
            local envs = package:envs()
            if envs then
                for _, name in ipairs({
                    "EMSDK",
                    "EMSDK_NODE",
                    "EMSDK_PYTHON",
                    "JAVA_HOME",
                }) do
                    local values = envs[name]
                    if values then
                        toolchain:add("runenvs", name, table.unwrap(values))
                    end
                end
            end
        end
    end)
toolchain_end()
