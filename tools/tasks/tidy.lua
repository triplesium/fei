import("async.runjobs")
import("core.base.task")
import("utils.progress")

local tooling = import("tasks.tooling", {
    rootdir = path.join(os.projectdir(), "tools")
})

local function make_compile_args(context)
    local compiler_instance = context.target:compiler(context.sourcekind)
    local args = compiler_instance:compflags({
        sourcefile = context.file,
        target = context.target,
    })
    for index, argument in ipairs(args) do
        args[index] = tostring(argument)
    end
    return args
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
