import("core.project.project")

local function normalize_path(filepath)
    return filepath:replace("\\", "/")
end

local function project_absolute_path(filepath)
    filepath = normalize_path(filepath)
    if path.is_absolute(filepath) then
        return filepath
    end
    return normalize_path(path.join(os.projectdir(), filepath))
end

local function insert_unique(values, value)
    value = project_absolute_path(value)
    if not table.contains(values, value) then
        table.insert(values, value)
    end
end

local function insert_unique_header(headers, header)
    if type(header) ~= "string" then
        return
    end
    local ext = path.extension(header)
    if ext ~= ".h" and ext ~= ".hh" and ext ~= ".hpp" and ext ~= ".hxx" then
        return
    end
    insert_unique(headers, header)
end

local function collect_package_include_dirs(include_dirs, target)
    for _, package in ipairs(target:orderpkgs()) do
        for _, incdir in ipairs(table.wrap(package:get("includedirs"))) do
            insert_unique(include_dirs, incdir)
        end
        for _, incdir in ipairs(table.wrap(package:get("sysincludedirs"))) do
            insert_unique(include_dirs, incdir)
        end
    end
end

local function collect_target_inputs(headers, include_dirs, target)
    for _, header in ipairs(target:headerfiles()) do
        insert_unique_header(headers, header)
    end
    for _, incdir in ipairs(table.wrap(target:get("includedirs"))) do
        insert_unique(include_dirs, incdir)
    end
    for _, incdir in ipairs(table.wrap(target:get("sysincludedirs"))) do
        insert_unique(include_dirs, incdir)
    end
    collect_package_include_dirs(include_dirs, target)
end

local function collect_reflgen_inputs()
    local generated_target = project.target("fei-generated")
    assert(generated_target, "target fei-generated not found")

    local headers = {}
    local include_dirs = {}
    for _, dep in ipairs(table.wrap(generated_target:get("deps"))) do
        local dep_target = assert(project.target(dep), "target " .. dep .. " not found")
        collect_target_inputs(headers, include_dirs, dep_target)
    end
    return headers, include_dirs
end

local function make_reflgen_args(headers, include_dirs)
    local args = {
        "--rootdir",
        normalize_path(path.join(os.projectdir(), "src")),
        "--output",
        normalize_path(path.join(os.projectdir(), "src/generated/reflgen.cpp"))
    }

    for _, incdir in ipairs(include_dirs) do
        table.insert(args, "-I")
        table.insert(args, incdir)
    end
    for _, header in ipairs(headers) do
        table.insert(args, header)
    end
    return args
end

function run()
    local headers, include_dirs = collect_reflgen_inputs()
    local args = {"run", "fei-reflgen"}
    table.join2(args, make_reflgen_args(headers, include_dirs))
    os.execv("xmake", args)
end
