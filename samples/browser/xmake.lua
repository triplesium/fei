function add_browser_shell(shell_file)
    add_ldflags(
        "--shell-file",
        shell_file,
        {force = true}
    )
    add_extrafiles(shell_file)

    before_build(function(target)
        local html_path = target:targetfile()
        if os.isfile(html_path) and os.mtime(shell_file) > os.mtime(html_path) then
            os.rm(html_path)
        end
    end)

    after_link(function(target)
        local html_path = target:targetfile()
        local html = io.readfile(html_path)
        local script_name = target:name() .. ".js"
        if html:find(script_name .. "?dev=", 1, true) then
            return
        end
        local script_pattern = script_name:gsub("(%W)", "%%%1")
        local script_tag = '<script async type="text/javascript" src="' ..
            script_pattern .. '"></script>'
        local script_loader = [[<script>
            const emscriptenScript = document.createElement("script");
            emscriptenScript.async = true;
            emscriptenScript.src = "]] .. script_name .. [[?dev=" + resourceVersion;
            document.body.appendChild(emscriptenScript);
        </script>]]
        local replacement_count
        html, replacement_count = html:gsub(script_tag, script_loader, 1)
        assert(
            replacement_count == 1,
            "failed to add cache busting to " .. script_name
        )
        io.writefile(html_path, html)
    end)
end

target("sample-browser")
    set_kind("binary")
    add_asset_bundle("browser", path.join(os.scriptdir(), "assets"))
    add_asset_bundle(
        "browser-fonts",
        path.join(os.scriptdir(), "../../engine/imgui/fonts")
    )
    add_files("main.cpp")
    add_deps(
        "entisium-core",
        "entisium-input",
        "entisium-input-focus",
        "entisium-sprite",
        "entisium-text",
        "entisium-ui",
        "entisium-ui-widgets",
        "entisium-ui-rendering",
        "entisium-window-browser",
        "entisium-graphics-webgpu-browser"
    )
    add_browser_shell(path.join(os.scriptdir(), "shell.html"))
