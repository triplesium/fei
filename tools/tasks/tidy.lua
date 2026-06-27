import("async.runjobs")
import("utils.progress")

local compile_commands_path = "build/compile_commands.json"
local tooling = import("tasks.tooling", {
    rootdir = path.join(os.projectdir(), "tools")
})

local function check_compile_commands()
    if not os.isfile(compile_commands_path) then
        raise(
            compile_commands_path ..
                " not found; run: xmake project -k compile_commands build"
        )
    end
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

local function make_clang_tidy_args(file, extra_args)
    local args = {"--quiet", "-p", "build"}
    table.join2(args, extra_args or {})
    table.insert(args, file)
    return args
end

local function run_clang_tidy_file(clang_tidy, file, progress_value, extra_args, verbose)
    progress.show(progress_value, "clang-tidy.analyzing %s", file)

    local result = {
        diagnostics = 0,
        failed = false,
        output = ""
    }

    try
    {
        function ()
            local args = make_clang_tidy_args(file, extra_args)
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

local function run_clang_tidy(clang_tidy, files, source_count, header_count, target_count, jobs, extra_args, verbose)
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
    check_compile_commands()
    local clang_tidy = tooling.find_program("clang-tidy")
    local files, source_count, header_count, target_count = tooling.collect_files(options.targets, options.files)

    run_clang_tidy(
        clang_tidy,
        files,
        source_count,
        header_count,
        target_count,
        options.jobs,
        {"--header-filter=^$"},
        options.verbose
    )
end
