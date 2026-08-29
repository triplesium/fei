import("async.runjobs")
import("core.base.task")
import("utils.progress")

local tooling = import("tasks.tooling", {
    rootdir = path.join(os.projectdir(), "tools")
})

local function compiler_program_name(compiler_instance)
    local program = path.filename(compiler_instance:program()):lower()
    program = program:gsub("%.exe$", "")
    program = program:gsub("%.bat$", "")
    program = program:gsub("%.cmd$", "")
    return program
end

local function compiler_driver_mode(compiler_instance, sourcekind)
    local program = compiler_program_name(compiler_instance)
    if program == "cl" or program == "clang-cl" then
        return "cl"
    end
    return sourcekind == "cc" and "gcc" or "g++"
end

local function is_emscripten_compiler(compiler_instance)
    local program = compiler_program_name(compiler_instance)
    return program == "emcc" or program == "em++"
end

local emscripten_port_include_cache = {}

local function emscripten_port_includes(compiler_instance, port_name)
    local emscripten_dir = path.directory(compiler_instance:program())
    local cache_key = emscripten_dir .. ":" .. port_name
    local cached = emscripten_port_include_cache[cache_key]
    if cached then
        return cached
    end

    local port_dir = path.join(emscripten_dir, "cache", "ports", port_name)
    local includes = os.dirs(path.join(port_dir, "**", "include"))
    table.sort(includes)
    emscripten_port_include_cache[cache_key] = includes
    return includes
end

local function insert_unique(arguments, seen, argument)
    if not seen[argument] then
        seen[argument] = true
        table.insert(arguments, argument)
    end
end

local function insert_system_include(arguments, seen, directory)
    if os.isdir(directory) then
        insert_unique(arguments, seen, "-isystem" .. directory)
    end
end

local function clang_frontend_argument(argument)
    -- These options are consumed by the emcc/em++ wrapper and are not Clang
    -- frontend arguments. Compilation-affecting options such as --target,
    --sysroot, defines, and include paths must remain intact.
    local emscripten_setting = argument == "-s" or
        argument:match("^%-s[A-Z0-9_]+=") ~= nil
    return not argument:startswith("--use-port=") and
        not argument:startswith("--preload-file=") and
        not argument:startswith("--shell-file=") and
        not emscripten_setting
end

local function make_compile_args(context)
    local compiler_instance = context.target:compiler(context.sourcekind)
    local args = compiler_instance:compflags({
        sourcefile = context.file,
        target = context.target,
    })
    local result = {}
    local seen = {}
    insert_unique(
        result,
        seen,
        "--driver-mode=" ..
            compiler_driver_mode(compiler_instance, context.sourcekind)
    )
    if is_emscripten_compiler(compiler_instance) then
        local arch = context.target:arch() == "wasm64" and "wasm64" or "wasm32"
        local emscripten_dir = path.directory(compiler_instance:program())
        local sysroot = path.join(emscripten_dir, "cache", "sysroot")
        local target_include = path.join(
            sysroot,
            "include",
            arch .. "-emscripten"
        )
        insert_unique(
            result,
            seen,
            "--target=" .. arch .. "-unknown-emscripten"
        )
        insert_unique(
            result,
            seen,
            "--sysroot=" .. sysroot
        )
        -- clang-tidy runs its own Clang binary, so it does not inherit the
        -- Emscripten driver's target-specific system include search paths.
        insert_system_include(
            result,
            seen,
            path.join(target_include, "noeh", "c++", "v1")
        )
        insert_system_include(
            result,
            seen,
            path.join(target_include, "c++", "v1")
        )
        insert_system_include(
            result,
            seen,
            path.join(sysroot, "include", "c++", "v1")
        )
        insert_system_include(result, seen, target_include)
        insert_system_include(result, seen, path.join(sysroot, "include"))
        insert_system_include(
            result,
            seen,
            path.join(sysroot, "include", "fakesdl")
        )
        insert_system_include(
            result,
            seen,
            path.join(sysroot, "include", "compat")
        )
        for _, argument in ipairs(args) do
            local port_name = tostring(argument):match(
                "^%-%-use%-port=([%w_%-]+)"
            )
            if port_name then
                for _, include_dir in ipairs(
                    emscripten_port_includes(compiler_instance, port_name)
                ) do
                    insert_system_include(result, seen, include_dir)
                end
            end
        end
    end
    local grouped_include_option
    for _, argument in ipairs(args) do
        argument = tostring(argument)
        if argument == "-isystem" or argument == "-iquote" or
            argument == "-idirafter" then
            grouped_include_option = argument
        elseif grouped_include_option and not argument:startswith("-") then
            insert_unique(result, seen, grouped_include_option .. argument)
        else
            grouped_include_option = nil
        end
        if not grouped_include_option and clang_frontend_argument(argument) then
            insert_unique(result, seen, argument)
        end
    end
    return result
end

local function count_diagnostics(output)
    local count = 0
    for line in output:gmatch("[^\r\n]+") do
        if line:find(":%d+:%d+:%s*warning:") or line:find(":%d+:%d+:%s*error:") then
            count = count + 1
        end
    end
    return count
end

local function make_clang_tidy_args(file, compile_args, extra_args)
    local args = {"--quiet"}
    table.join2(args, extra_args or {})
    if is_host("windows") then
        for _, argument in ipairs(compile_args) do
            table.insert(args, "--extra-arg-before=" .. argument)
        end
        table.insert(args, file)
        table.insert(args, "--")
        return args
    end

    table.insert(args, file)
    table.insert(args, "--")
    table.join2(args, compile_args)
    return args
end

local function run_clang_tidy_file(
    clang_tidy,
    file,
    compile_args,
    progress_value,
    extra_args,
    verbose
)
    progress.show(progress_value, "clang-tidy.analyzing %s", file)

    local result = {
        diagnostics = 0,
        failed = false,
        output = ""
    }

    try
    {
        function ()
            local args = make_clang_tidy_args(file, compile_args, extra_args)
            if verbose then
                progress.show_output("${dim}%s %s", clang_tidy, os.args(args))
            end
            local outdata, errdata = os.iorunv(clang_tidy, args, {
                curdir = os.projectdir()
            })
            result.output = (outdata or "") .. (errdata or "")
        end,
        catch
        {
            function (errors)
                result.failed = true
                result.output = tooling.error_text(errors, "check failed")
            end
        },
        finally
        {
            function ()
                if result.output and #result.output:trim() > 0 then
                    result.diagnostics = count_diagnostics(result.output)
                    if result.failed then
                        progress.show_output("${color.error}%s:\n%s", file, result.output)
                    else
                        progress.show_output("${color.warning}%s:\n%s", file, result.output)
                    end
                end
            end
        }
    }
    return result
end

local function run_clang_tidy(
    clang_tidy,
    files,
    compile_args,
    source_count,
    header_count,
    target_count,
    jobs,
    extra_args,
    verbose
)
    if #files == 0 then
        print("clang-tidy: no files found")
        return
    end

    jobs = tooling.job_count(jobs)

    if verbose then
        print("clang-tidy: using " .. clang_tidy)
    end
    print(
        "clang-tidy: checking " ..
            tooling.file_counts(#files, source_count, header_count) ..
            " from " ..
            tostring(target_count) ..
            " targets" ..
            " with " ..
            tostring(jobs) ..
            " jobs"
    )

    local stats = {
        diagnostics = 0,
        failed = 0
    }
    local analyze_time = os.mclock()
    runjobs("tidy", function (index, total, opt)
        local result = run_clang_tidy_file(
            clang_tidy,
            files[index],
            compile_args[files[index]],
            opt.progress,
            extra_args,
            verbose
        )
        stats.diagnostics = stats.diagnostics + result.diagnostics
        if result.failed then
            stats.failed = stats.failed + 1
        end
    end, {
        total = #files,
        comax = jobs,
        showtips = false,
        progress_refresh = true
    })
    analyze_time = os.mclock() - analyze_time
    progress.show(
        100,
        "${color.success}clang-tidy: checked %d files, diagnostics %d, failed %d, spent %.3fs",
        #files,
        stats.diagnostics,
        stats.failed,
        analyze_time / 1000
    )
    if stats.failed > 0 then
        raise("clang-tidy failed for %d file(s)", stats.failed)
    end
end

function run(options)
    options = options or {}
    task.run("config", {}, {loadonly = true})
    local clang_tidy = tooling.find_program("clang-tidy")
    local files, source_count, header_count, target_count, file_contexts =
        tooling.collect_files(options.targets, options.files)
    local compile_args = {}
    for _, file in ipairs(files) do
        compile_args[file] = make_compile_args(file_contexts[file])
    end
    local extra_args = {"--header-filter=^$"}
    if options.fix then
        table.join2(extra_args, {"--fix", "--format-style=file"})
    end

    run_clang_tidy(
        clang_tidy,
        files,
        compile_args,
        source_count,
        header_count,
        target_count,
        options.jobs,
        extra_args,
        options.verbose
    )
end
