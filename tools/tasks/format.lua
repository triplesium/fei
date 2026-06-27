import("async.runjobs")
import("utils.progress")

local tooling = import("tasks.tooling", {
    rootdir = path.join(os.projectdir(), "tools")
})

local function make_clang_format_args(file, check)
    local args = {"--style=file", "--fallback-style=none"}
    if check then
        table.join2(args, {"--dry-run", "--Werror"})
    else
        table.insert(args, "-i")
    end
    table.insert(args, file)
    return args
end

local function run_clang_format_file(clang_format, file, check, progress_value, verbose)
    local action = check and "checking" or "formatting"
    progress.show(progress_value, "clang-format.%s %s", action, file)

    local result = {
        failed = false,
        output = ""
    }

    try
    {
        function ()
            local args = make_clang_format_args(file, check)
            if verbose then
                progress.show_output("${dim}%s %s", clang_format, os.args(args))
            end
            local outdata, errdata = os.iorunv(clang_format, args, {
                curdir = os.projectdir()
            })
            result.output = (outdata or "") .. (errdata or "")
        end,
        catch
        {
            function (errors)
                result.failed = true
                result.output = tooling.error_text(errors, "format failed")
            end
        },
        finally
        {
            function ()
                if result.output and #result.output:trim() > 0 then
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

local function run_clang_format(clang_format, files, source_count, header_count, target_count, jobs, check, verbose)
    if #files == 0 then
        print("clang-format: no files found")
        return
    end

    jobs = tooling.job_count(jobs)

    local action = check and "checking " or "formatting "
    if verbose then
        print("clang-format: using " .. clang_format)
    end
    print(
        "clang-format: " ..
            action ..
            tooling.file_counts(#files, source_count, header_count) ..
            " from " ..
            tostring(target_count) ..
            " targets" ..
            " with " ..
            tostring(jobs) ..
            " jobs"
    )

    local stats = {
        failed = 0
    }
    local format_time = os.mclock()
    runjobs("format", function (index, total, opt)
        local result = run_clang_format_file(
            clang_format,
            files[index],
            check,
            opt.progress,
            verbose
        )
        if result.failed then
            stats.failed = stats.failed + 1
        end
    end, {
        total = #files,
        comax = jobs,
        showtips = false,
        progress_refresh = true
    })
    format_time = os.mclock() - format_time
    progress.show(
        100,
        "${color.success}clang-format: %s %d files, failed %d, spent %.3fs",
        check and "checked" or "formatted",
        #files,
        stats.failed,
        format_time / 1000
    )
    if stats.failed > 0 then
        raise("clang-format failed for %d file(s)", stats.failed)
    end
end

function run(options)
    options = options or {}
    local clang_format = tooling.find_program("clang-format")
    local files, source_count, header_count, target_count = tooling.collect_files(options.targets, options.files)

    run_clang_format(
        clang_format,
        files,
        source_count,
        header_count,
        target_count,
        options.jobs,
        options.check,
        options.verbose
    )
end
