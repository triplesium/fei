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

local function should_skip_file(filepath)
    return filepath:startswith("src/generated/")
end

local function insert_unique(files, seen, filepath)
    filepath = project_relative_path(filepath)
    if not should_skip_file(filepath) and not seen[filepath] then
        seen[filepath] = true
        table.insert(files, filepath)
        return true
    end
    return false
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

function collect_files(target_names)
    local targets = collect_targets(target_names)
    local files = {}
    local seen = {}
    local source_count = 0
    local header_count = 0

    for _, target in ipairs(targets) do
        for _, sourcebatch in pairs(target:sourcebatches()) do
            if source_batch_should_include(sourcebatch) then
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
