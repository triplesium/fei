import("async.runjobs")
import("core.project.project")
import("lib.detect.find_tool")
import("utils.progress")

local compile_commands_path = "build/compile_commands.json"
local source_rules = {
    ["c.build"] = true,
    ["c++.build"] = true,
    ["c++.build.modules"] = true,
    ["cuda.build"] = true,
    ["objc.build"] = true,
    ["objc++.build"] = true,
}

local function normalize_path(filepath)
    return filepath:replace("\\", "/")
end

local function project_relative_path(filepath)
    filepath = normalize_path(filepath)
    local project_dir = normalize_path(os.projectdir())
    if filepath:startswith(project_dir .. "/") then
        filepath = filepath:sub(#project_dir + 2)
    end
    return filepath
end

local function is_generated_file(filepath)
    return project_relative_path(filepath):startswith("src/generated/")
end

local function insert_unique(files, seen, filepath)
    filepath = project_relative_path(filepath)
    if not is_generated_file(filepath) and not seen[filepath] then
        seen[filepath] = true
        table.insert(files, filepath)
        return true
    end
    return false
end

local function check_compile_commands()
    if not os.isfile(compile_commands_path) then
        raise(
            compile_commands_path ..
                " not found; run: xmake project -k compile_commands build"
        )
    end
end

local function source_batch_should_tidy(sourcebatch)
    return source_rules[sourcebatch.rulename]
end

local function normalize_options(options)
    local normalized = {
        jobs = options.jobs,
        targets = {},
        verbose = options.verbose,
    }

    local target_args = table.wrap(options.targets)
    local index = 1
    while index <= #target_args do
        local argument = target_args[index]
        if argument == "-j" or argument == "--jobs" then
            normalized.jobs = target_args[index + 1]
            if not normalized.jobs then
                raise("%s requires a value", argument)
            end
            index = index + 2
        elseif argument:startswith("--jobs=") then
            normalized.jobs = argument:sub(#"--jobs=" + 1)
            index = index + 1
        elseif argument:startswith("-j") and #argument > 2 then
            normalized.jobs = argument:sub(3)
            index = index + 1
        elseif argument == "-v" or argument == "--verbose" then
            normalized.verbose = true
            index = index + 1
        else
            table.insert(normalized.targets, argument)
            index = index + 1
        end
    end

    if #normalized.targets == 0 then
        normalized.targets = nil
    end
    return normalized
end

local function collect_targets(target_names)
    local targets = {}
    if target_names then
        for _, target_name in ipairs(table.wrap(target_names)) do
            table.insert(
                targets,
                assert(project.target(target_name), "unknown target(%s)", target_name)
            )
        end
    else
        for _, target in ipairs(project.ordertargets()) do
            table.insert(targets, target)
        end
    end
    return targets
end

local function collect_tidy_files(target_names)
    local targets = collect_targets(target_names)
    local files = {}
    local seen = {}
    local source_count = 0
    local header_count = 0

    for _, target in ipairs(targets) do
        for _, sourcebatch in pairs(target:sourcebatches()) do
            if source_batch_should_tidy(sourcebatch) then
                for _, source in ipairs(sourcebatch.sourcefiles) do
                    if insert_unique(files, seen, source) then
                        source_count = source_count + 1
                    end
                end
            end
        end

        for _, header in ipairs(target:headerfiles()) do
            if insert_unique(files, seen, header) then
                header_count = header_count + 1
            end
        end
    end

    table.sort(files)
    return files, source_count, header_count, #targets
end

local function find_clang_tidy()
    local clang_tidy = find_tool("clang-tidy")
    assert(clang_tidy, "clang-tidy not found!")
    return clang_tidy.program
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

local function get_error_text(errors)
    if type(errors) == "table" then
        local error_text = (errors.stdout or "") .. (errors.stderr or "")
        if #error_text:trim() == 0 then
            error_text = errors.errors or "check failed"
        end
        return error_text
    end
    return tostring(errors)
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
                result.output = get_error_text(errors)
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

local function format_file_counts(total, source_count, header_count)
    if header_count > 0 then
        return format(
            "%d files (%d sources, %d headers)",
            total,
            source_count,
            header_count
        )
    end
    return format("%d files (%d sources)", total, source_count)
end

local function run_clang_tidy(clang_tidy, files, source_count, header_count, target_count, jobs, extra_args, verbose)
    if #files == 0 then
        print("clang-tidy: no files found")
        return
    end

    jobs = tonumber(jobs) or os.default_njob()
    if jobs <= 0 then
        jobs = 1
    end

    if verbose then
        print("clang-tidy: using " .. clang_tidy)
    end
    print(
        "clang-tidy: checking " ..
            format_file_counts(#files, source_count, header_count) ..
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
    options = normalize_options(options or {})
    check_compile_commands()
    local clang_tidy = find_clang_tidy()
    local files, source_count, header_count, target_count = collect_tidy_files(options.targets)

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
