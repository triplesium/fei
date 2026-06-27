import("core.project.project")
import("lib.detect.find_tool")

local source_rules = {
    ["c.build"] = true,
    ["c++.build"] = true,
    ["c++.build.modules"] = true,
    ["cuda.build"] = true,
    ["objc.build"] = true,
    ["objc++.build"] = true,
}

local function project_relative_path(filepath)
    return path.unix(path.relative(filepath, os.projectdir()))
end

local function normalize_file_pattern(pattern)
    pattern = path.unix(tostring(pattern):trim())
    if #pattern == 0 then
        return nil
    end

    local projectdir = path.unix(os.projectdir()):gsub("/+$", "")
    local comparable_pattern = pattern
    local comparable_projectdir = projectdir
    if os.host() == "windows" then
        comparable_pattern = comparable_pattern:lower()
        comparable_projectdir = comparable_projectdir:lower()
    end

    if comparable_pattern == comparable_projectdir then
        return "."
    end
    if comparable_pattern:startswith(comparable_projectdir .. "/") then
        pattern = pattern:sub(#projectdir + 2)
    end

    return pattern:gsub("^%./", "")
end

local function collect_file_patterns(patterns)
    local result = {}
    for _, pattern in ipairs(table.wrap(patterns)) do
        for _, part in ipairs(path.splitenv(tostring(pattern))) do
            part = normalize_file_pattern(part)
            if part then
                table.insert(result, path.pattern(part))
            end
        end
    end
    return result
end

local function should_skip_file(filepath)
    return filepath:startswith("build/.gens/")
end

local function insert_unique(files, seen, filepath)
    filepath = project_relative_path(filepath)
    if not should_skip_file(filepath) and not seen[filepath] then
        seen[filepath] = true
        table.insert(files, filepath)
        return filepath
    end
    return nil
end

local function source_batch_should_include(sourcebatch)
    return source_rules[sourcebatch.rulename]
end

local function collect_targets(target_names)
    local targets = {}
    target_names = table.wrap(target_names)
    if #target_names > 0 then
        for _, target_name in ipairs(target_names) do
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

local function count_files(files, file_kinds)
    local source_count = 0
    local header_count = 0
    for _, file in ipairs(files) do
        if file_kinds[file] == "header" then
            header_count = header_count + 1
        else
            source_count = source_count + 1
        end
    end
    return source_count, header_count
end

local function filter_files(files, file_kinds, patterns)
    patterns = collect_file_patterns(patterns)
    if #patterns == 0 then
        local source_count, header_count = count_files(files, file_kinds)
        return files, source_count, header_count
    end

    local filtered = {}
    for _, file in ipairs(files) do
        for _, pattern in ipairs(patterns) do
            if file:match(pattern) == file then
                table.insert(filtered, file)
                break
            end
        end
    end

    local source_count, header_count = count_files(filtered, file_kinds)
    return filtered, source_count, header_count
end

function collect_files(target_names, file_patterns)
    local targets = collect_targets(target_names)
    local files = {}
    local file_kinds = {}
    local seen = {}

    for _, target in ipairs(targets) do
        for _, sourcebatch in pairs(target:sourcebatches()) do
            if source_batch_should_include(sourcebatch) then
                for _, source in ipairs(sourcebatch.sourcefiles) do
                    local file = insert_unique(files, seen, source)
                    if file then
                        file_kinds[file] = "source"
                    end
                end
            end
        end

        for _, header in ipairs(target:headerfiles()) do
            local file = insert_unique(files, seen, header)
            if file then
                file_kinds[file] = "header"
            end
        end
    end

    table.sort(files)
    local source_count, header_count
    files, source_count, header_count = filter_files(files, file_kinds, file_patterns)
    return files, source_count, header_count, #targets
end

function find_program(name)
    local program = find_tool(name)
    assert(program, "%s not found!", name)
    return program.program
end

function job_count(jobs)
    jobs = tonumber(jobs) or os.default_njob()
    if jobs <= 0 then
        jobs = 1
    end
    return jobs
end

function file_counts(total, source_count, header_count)
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

function error_text(errors, fallback)
    if type(errors) == "table" then
        local text = (errors.stdout or "") .. (errors.stderr or "")
        if #text:trim() == 0 then
            text = errors.errors or fallback
        end
        return text
    end
    return tostring(errors)
end
