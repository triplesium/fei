import("core.project.project")

local function normalize_path(filepath)
    return filepath:replace("\\", "/")
end

local function insert_unique(values, value)
    value = normalize_path(value)
    if not table.contains(values, value) then
        table.insert(values, value)
    end
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
        insert_unique(headers, header)
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
        "tools/reflgen/reflgen.py",
        "--rootdir",
        "src",
        "-t",
        "tools/reflgen/reflgen.jinja",
        "--output",
        "src/generated/reflgen.cpp"
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
    os.execv("python3", make_reflgen_args(headers, include_dirs))
end
