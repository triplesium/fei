target("sample-browser")
    set_kind("binary")
    add_asset_bundle("browser", path.join(os.scriptdir(), "assets"))
    add_asset_bundle(
        "browser-fonts",
        path.join(os.scriptdir(), "../../engine/imgui/fonts")
    )
    add_files("main.cpp")
    add_deps(
        "fei-core",
        "fei-input",
        "fei-input-focus",
        "fei-sprite",
        "fei-text",
        "fei-ui",
        "fei-ui-widgets",
        "fei-ui-rendering",
        "fei-window-browser",
        "fei-graphics-webgpu-browser"
    )
    add_ldflags(
        "--shell-file",
        path.join(os.scriptdir(), "shell.html"),
        {force = true}
    )
    add_extrafiles("shell.html")

    after_link(function(target)
        local html_path = target:targetfile()
        local html = io.readfile(html_path)
        if html:find("sample-browser.js?dev=${resourceVersion}", 1, true) then
            return
        end
        local script_tag = '<script async type="text/javascript" src="sample%-browser%.js"></script>'
        local script_loader = [[<script>
            const emscriptenScript = document.createElement("script");
            emscriptenScript.async = true;
            emscriptenScript.src = `sample-browser.js?dev=${resourceVersion}`;
            document.body.appendChild(emscriptenScript);
        </script>]]
        local replacement_count
        html, replacement_count = html:gsub(script_tag, script_loader, 1)
        assert(replacement_count == 1, "failed to add cache busting to sample-browser.js")
        io.writefile(html_path, html)
    end)
