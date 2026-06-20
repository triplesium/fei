import("core.project.depend")
import("core.project.project")

local function normalize_path(filepath)
    local normalized = tostring(filepath):gsub("\\", "/")
    return normalized
end

local function project_absolute_path(filepath)
    filepath = normalize_path(filepath)
    if path.is_absolute(filepath) then
        return filepath
    end
    return normalize_path(path.join(os.projectdir(), filepath))
end

local function project_relative_path(filepath)
    local absolute = project_absolute_path(filepath)
    local root = normalize_path(os.projectdir())
    if absolute == root then
        return ""
    end
    if absolute:sub(1, #root + 1) == root .. "/" then
        return absolute:sub(#root + 2)
    end
    return absolute
end

local function ensure_directory(directory)
    directory = normalize_path(directory)
    if #directory == 0 or os.isdir(directory) then
        return
    end

    local parent = path.directory(directory)
    if parent and parent ~= directory and parent ~= "." and #parent > 0 then
        ensure_directory(parent)
    end
    os.mkdir(directory)
end

local function write_file_if_changed(filepath, content)
    ensure_directory(path.directory(project_absolute_path(filepath)))
    if os.isfile(filepath) and io.readfile(filepath) == content then
        return
    end
    io.writefile(filepath, content)
end

local function insert_unique(values, value)
    if type(value) ~= "string" or #value == 0 then
        return
    end
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

local function safe_identifier(value)
    value = normalize_path(value):gsub("[^%w_]", "_")
    value = value:gsub("_+", "_")
    value = value:gsub("^_", ""):gsub("_$", "")
    if #value == 0 then
        return "file"
    end
    return value
end

local function target_symbol_name(target_name)
    return "register_" .. safe_identifier(target_name) .. "_reflection"
end

local function reflected_target_enabled(target)
    return target:values("fei.reflect") == true
end

local function reflect_link_owner(kind)
    return kind == "binary" or kind == "shared"
end

local function header_has_reflect_marker(header)
    if not os.isfile(header) then
        return false
    end
    local content = io.readfile(header)
    return content and content:find("FEI_REFLECT", 1, true) ~= nil
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

local function collect_target_include_dirs(include_dirs, target)
    for _, incdir in ipairs(table.wrap(target:get("includedirs"))) do
        insert_unique(include_dirs, incdir)
    end
    for _, incdir in ipairs(table.wrap(target:get("sysincludedirs"))) do
        insert_unique(include_dirs, incdir)
    end
    collect_package_include_dirs(include_dirs, target)
end

local function collect_recursive_include_dirs(include_dirs, target, visited)
    visited = visited or {}
    if visited[target:name()] then
        return
    end
    visited[target:name()] = true

    collect_target_include_dirs(include_dirs, target)
    for _, dep in ipairs(table.wrap(target:get("deps"))) do
        if dep ~= "fei-reflgen" then
            local dep_target = project.target(dep)
            if dep_target then
                collect_recursive_include_dirs(include_dirs, dep_target, visited)
            end
        end
    end
end

local function collect_target_headers(headers, target)
    for _, header in ipairs(target:headerfiles()) do
        insert_unique_header(headers, header)
    end
    for _, header in ipairs(table.wrap(target:values("fei.reflect.headers"))) do
        insert_unique_header(headers, header)
    end
end

local function reflection_headers(target)
    local headers = {}
    collect_target_headers(headers, target)
    table.sort(headers)

    local reflected_headers = {}
    for _, header in ipairs(headers) do
        if header_has_reflect_marker(header) then
            table.insert(reflected_headers, header)
        end
    end
    return reflected_headers
end

local function file_entries(target)
    local autogendir = target:values("fei.reflect.dir")
    if not autogendir then
        autogendir = path.join(os.projectdir(), "build/.gens", target:name(), "reflection")
    end

    local entries = {}
    local used_names = {}
    local target_name = safe_identifier(target:name())
    for _, header in ipairs(reflection_headers(target)) do
        local relative = project_relative_path(header)
        local stable = safe_identifier(relative)
        local base = stable
        local index = 2
        while used_names[stable] do
            stable = base .. "_" .. index
            index = index + 1
        end
        used_names[stable] = true

        table.insert(entries, {
            header = header,
            header_relative = relative,
            stable = stable,
            marker = path.join(autogendir, "files", stable .. ".reflgen"),
            output_file = path.join(autogendir, "files", stable .. ".cpp"),
            function_name = "register_" .. target_name .. "_" .. stable .. "_reflection"
        })
    end
    return entries
end

local function collect_reflected_targets(root_target)
    local reflected_targets = {}
    local visited = {}

    local function visit(target)
        if not target or visited[target:name()] then
            return
        end
        visited[target:name()] = true

        for _, dep in ipairs(table.wrap(target:get("deps"))) do
            if dep ~= "fei-reflgen" then
                visit(project.target(dep))
            end
        end

        if reflected_target_enabled(target) then
            table.insert(reflected_targets, target)
        end
    end

    visit(root_target)
    return reflected_targets
end

local function comparable_file_path(filepath)
    local comparable = normalize_path(project_absolute_path(filepath))
    if is_plat("windows") then
        comparable = comparable:lower()
    end
    return comparable
end

local function target_dependency_closure(target)
    local deps = {}

    local function visit(dep_name)
        if dep_name == "fei-reflgen" or deps[dep_name] then
            return
        end

        local dep_target = project.target(dep_name)
        if not dep_target then
            return
        end

        deps[dep_name] = true
        for _, child_dep in ipairs(table.wrap(dep_target:get("deps"))) do
            visit(child_dep)
        end
    end

    for _, dep in ipairs(table.wrap(target:get("deps"))) do
        visit(dep)
    end
    return deps
end

local function project_header_owners()
    local owners = {}
    for _, target in ipairs(project.ordertargets()) do
        local headers = {}
        collect_target_headers(headers, target)
        for _, header in ipairs(headers) do
            local key = comparable_file_path(header)
            if not owners[key] then
                owners[key] = target
            end
        end
    end
    return owners
end

local function project_local_file(filepath)
    local absolute = comparable_file_path(filepath)
    local root = comparable_file_path(os.projectdir())
    return absolute == root or absolute:sub(1, #root + 1) == root .. "/"
end

local function include_directives(header)
    local includes = {}
    local content = io.readfile(header)
    if not content then
        return includes
    end

    for line in content:gmatch("[^\r\n]+") do
        local include = line:match("^%s*#%s*include%s*[<\"]([^>\"]+)[>\"]")
        if include then
            table.insert(includes, include)
        end
    end
    return includes
end

local function resolve_project_include(include, header, include_dirs)
    local candidates = {}
    if path.is_absolute(include) then
        table.insert(candidates, include)
    else
        table.insert(candidates, path.join(path.directory(header), include))
        for _, include_dir in ipairs(include_dirs) do
            table.insert(candidates, path.join(include_dir, include))
        end
    end

    for _, candidate in ipairs(candidates) do
        if os.isfile(candidate) and project_local_file(candidate) then
            return project_absolute_path(candidate)
        end
    end
    return nil
end

local function validate_reflect_dependencies(target, inputs)
    if #inputs.headers == 0 then
        return
    end

    local owners = project_header_owners()
    local deps = target_dependency_closure(target)
    local reported = {}
    for _, header in ipairs(inputs.headers) do
        for _, include in ipairs(include_directives(header)) do
            local included_file =
                resolve_project_include(include, header, inputs.include_dirs)
            if included_file then
                local owner = owners[comparable_file_path(included_file)]
                if owner and owner:name() ~= target:name() and not deps[owner:name()] then
                    local report_key = comparable_file_path(header) .. "->" .. comparable_file_path(included_file)
                    if not reported[report_key] then
                        reported[report_key] = true
                        raise(
                            "fei.reflect: %s includes %s, but target %s does not depend on %s",
                            project_relative_path(header),
                            project_relative_path(included_file),
                            target:name(),
                            owner:name()
                        )
                    end
                end
            end
        end
    end
end

local function reflgen_sources()
    local files = {}
    for _, file in ipairs(os.files(path.join(os.projectdir(), "tools/reflgen/*.cpp"))) do
        insert_unique(files, file)
    end
    for _, file in ipairs(os.files(path.join(os.projectdir(), "tools/reflgen/*.hpp"))) do
        insert_unique(files, file)
    end
    insert_unique(files, path.join(os.projectdir(), "tools/reflgen/rules.lua"))
    return files
end

local function reflection_runtime_headers()
    -- Generated reflection files instantiate MethodImpl/PropertyImpl templates.
    -- Rebuild them whenever the reflection runtime ABI changes.
    local files = {}
    for _, file in ipairs(os.files(path.join(os.projectdir(), "src/refl/**.hpp"))) do
        if not normalize_path(file):find("/tests/", 1, true) then
            insert_unique(files, file)
        end
    end
    insert_unique(files, path.join(os.projectdir(), "src/base/result.hpp"))
    return files
end

local function reflgen_program()
    local target = assert(project.target("fei-reflgen"), "target fei-reflgen not found")
    local program = target:targetfile()
    if os.isfile(program) then
        return program
    end

    local filename = path.filename(program)
    for _, candidate in ipairs(os.files(path.join(os.projectdir(), "build/**/" .. filename))) do
        if os.isfile(candidate) then
            return candidate
        end
    end
    raise("missing fei-reflgen executable: %s", program)
end

local function reflgen_depfile(entry)
    return project_absolute_path(entry.output_file .. ".mk.d")
end

local function reflgen_dep_target(entry)
    return normalize_path(project_relative_path(entry.output_file))
end

local function unescape_make_depfile_token(token)
    token = token:gsub("%$%$", "$")
    token = token:gsub("\\ ", " ")
    token = token:gsub("\\#", "#")
    token = token:gsub("\\:", ":")
    token = token:gsub("\\\\", "\\")
    return token
end

local function parse_reflgen_depfile(depfile, fallback_files)
    local depfiles = {}
    for _, file in ipairs(fallback_files or {}) do
        insert_unique(depfiles, file)
    end

    if not os.isfile(depfile) then
        return depfiles
    end

    local content = io.readfile(depfile)
    if not content then
        return depfiles
    end
    content = content:gsub("\\\r?\n", " ")

    local colon = content:find(":", 1, true)
    if not colon then
        return depfiles
    end

    local body = content:sub(colon + 1)
    for token in body:gmatch("%S+") do
        insert_unique(depfiles, unescape_make_depfile_token(token))
    end
    return depfiles
end

local function make_reflgen_args(
    headers,
    include_dirs,
    output_file,
    function_name,
    stamp_file,
    depfile,
    dep_target
)
    local args = {
        "--rootdir",
        normalize_path(os.projectdir()),
        "--output",
        normalize_path(output_file),
        "--function",
        function_name
    }

    if stamp_file then
        table.insert(args, "--stamp")
        table.insert(args, normalize_path(stamp_file))
    end
    if depfile then
        table.insert(args, "--depfile")
        table.insert(args, normalize_path(depfile))
    end
    if dep_target then
        table.insert(args, "--dep-target")
        table.insert(args, normalize_path(dep_target))
    end
    for _, incdir in ipairs(include_dirs) do
        table.insert(args, "-I")
        table.insert(args, incdir)
    end
    for _, header in ipairs(headers) do
        table.insert(args, header)
    end
    return args
end

local function module_inputs(target)
    local include_dirs = {}
    insert_unique(include_dirs, os.projectdir())
    insert_unique(include_dirs, path.join(os.projectdir(), "src"))
    collect_recursive_include_dirs(include_dirs, target)
    return {
        module_file = target:values("fei.reflect.module_file"),
        module_function = target:values("fei.reflect.module_function"),
        module_marker_file = target:values("fei.reflect.module_marker_file"),
        headers = reflection_headers(target),
        include_dirs = include_dirs,
        entries = file_entries(target)
    }
end

local function depfiles_with_reflgen(files)
    local depfiles = {}
    for _, file in ipairs(files) do
        insert_unique(depfiles, file)
    end
    for _, file in ipairs(reflection_runtime_headers()) do
        insert_unique(depfiles, file)
    end
    for _, file in ipairs(reflgen_sources()) do
        insert_unique(depfiles, file)
    end
    return depfiles
end

local function aggregate_inputs(target)
    local output_file = target:values("fei.reflect.aggregate_file")
    local functions = {}
    local files = {}
    for _, reflected_target in ipairs(collect_reflected_targets(target)) do
        local function_name = reflected_target:values("fei.reflect.module_function")
        local module_file = reflected_target:values("fei.reflect.module_file")
        if function_name then
            table.insert(functions, function_name)
        end
        if module_file then
            insert_unique(files, module_file)
        end
    end
    for _, file in ipairs(reflgen_sources()) do
        insert_unique(files, file)
    end
    for _, file in ipairs(reflection_runtime_headers()) do
        insert_unique(files, file)
    end
    return output_file, functions, files
end

local function write_stamp_file(stamp_file)
    if not stamp_file then
        return
    end
    ensure_directory(path.directory(project_absolute_path(stamp_file)))
    io.writefile(stamp_file, tostring(os.time()) .. "\n")
end

local function cleanup_legacy_runner_files(target)
    local autogendir = target:values("fei.reflect.dir")
    if not autogendir or not os.isdir(autogendir) then
        return
    end

    local prefix = "fei-reflgen-" .. target:name()
    for _, directory in ipairs({autogendir, path.join(autogendir, "files")}) do
        if os.isdir(directory) then
            for _, filepath in ipairs(os.files(path.join(directory, prefix .. "*"))) do
                if os.isfile(filepath) and path.filename(filepath):sub(1, #prefix) == prefix then
                    os.rm(filepath)
                end
            end
        end
    end

    local old_types_stamp = path.join(autogendir, "types.txt.stamp")
    if os.isfile(old_types_stamp) then
        os.rm(old_types_stamp)
    end
end

local function write_module_aggregate(output_file, module_function, functions)
    local lines = {
        "// This file is generated by fei-reflgen",
        "",
        "#include \"refl/registry.hpp\"",
        "",
        "namespace fei::refl::generated {"
    }

    for _, function_name in ipairs(functions) do
        table.insert(lines, format("void %s(Registry& registry);", function_name))
    end

    table.insert(lines, "")
    table.insert(lines, format("void %s(Registry& registry) {", module_function))
    for _, function_name in ipairs(functions) do
        table.insert(lines, format("    %s(registry);", function_name))
    end
    table.insert(lines, "}")
    table.insert(lines, "")
    table.insert(lines, "} // namespace fei::refl::generated")
    table.insert(lines, "")

    write_file_if_changed(output_file, table.concat(lines, "\n"))
end

local function write_aggregate(output_file, functions)
    local lines = {
        "// This file is generated by fei-reflgen",
        "",
        "#include \"refl/generated.hpp\"",
        "#include \"refl/registry.hpp\"",
        "",
        "namespace fei::refl::generated {"
    }

    for _, function_name in ipairs(functions) do
        table.insert(lines, format("void %s(Registry& registry);", function_name))
    end

    table.insert(lines, "} // namespace fei::refl::generated")
    table.insert(lines, "")
    table.insert(lines, "namespace fei {")
    table.insert(lines, "")
    table.insert(lines, "void register_generated_reflection() {")
    table.insert(lines, "    auto& registry = Registry::instance();")

    for _, function_name in ipairs(functions) do
        table.insert(lines, format("    refl::generated::%s(registry);", function_name))
    end

    table.insert(lines, "}")
    table.insert(lines, "")
    table.insert(lines, "} // namespace fei")
    table.insert(lines, "")

    write_file_if_changed(output_file, table.concat(lines, "\n"))
end

local function entry_for_marker(target, sourcefile)
    local wanted = project_absolute_path(sourcefile)
    for _, entry in ipairs(file_entries(target)) do
        if project_absolute_path(entry.marker) == wanted then
            return entry
        end
    end
    return nil
end

function configure_target(target)
    local autogendir = path.join(os.projectdir(), "build/.gens", target:name(), "reflection")
    target:set("values", "fei.reflect", true)
    target:set("values", "fei.reflect.dir", autogendir)
    target:set("values", "fei.reflect.module_file", path.join(autogendir, "reflection.cpp"))
    target:set("values", "fei.reflect.module_function", target_symbol_name(target:name()))
    target:set("values", "fei.reflect.module_marker_file", path.join(autogendir, "module.reflmod"))
    target:add("deps", "fei-reflgen", {links = false})

    for _, entry in ipairs(file_entries(target)) do
        target:add("files", entry.marker, {always_added = true})
    end

    target:add("files", target:values("fei.reflect.module_marker_file"), {always_added = true})
    target:add("includedirs", os.projectdir())
    target:add("cxxflags", "cl::/bigobj")

    if reflect_link_owner(target:kind()) then
        target:set("values", "fei.reflect.aggregate_file", path.join(autogendir, "reflection_aggregate.cpp"))
        target:set("values", "fei.reflect.aggregate_marker_file", path.join(autogendir, "aggregate.reflagg"))
        target:add("files", target:values("fei.reflect.aggregate_marker_file"), {always_added = true})
    end
end

function clean(target)
    if not reflected_target_enabled(target) then
        return
    end

    local target_gens_dir = project_absolute_path(path.join(os.projectdir(), "build/.gens", target:name()))
    local function remove_reflection_dir(directory)
        local absolute = project_absolute_path(directory)
        if absolute == target_gens_dir or absolute:sub(1, #target_gens_dir + 1) ~= target_gens_dir .. "/" then
            raise("refusing to clean unexpected reflection directory: %s", absolute)
        end
        os.rm(absolute)
    end

    local autogendir = target:values("fei.reflect.dir")
    if autogendir and os.isdir(autogendir) then
        remove_reflection_dir(autogendir)
    end

    if os.isdir(target_gens_dir) then
        for _, directory in ipairs(os.dirs(path.join(target_gens_dir, "**", "reflection"))) do
            remove_reflection_dir(directory)
        end
        if #os.files(path.join(target_gens_dir, "**")) == 0 then
            os.rm(target_gens_dir)
        end
    end
end

local function generate_files(target)
    if not reflected_target_enabled(target) then
        return
    end

    local inputs = module_inputs(target)
    validate_reflect_dependencies(target, inputs)
    cleanup_legacy_runner_files(target)
    for _, entry in ipairs(inputs.entries) do
        local dependfile = project_absolute_path(entry.output_file .. ".d")
        local header_depfile = reflgen_depfile(entry)
        ensure_directory(path.directory(dependfile))

        depend.on_changed(function ()
            os.vrunv(
                reflgen_program(),
                make_reflgen_args(
                    {entry.header},
                    inputs.include_dirs,
                    entry.output_file,
                    entry.function_name,
                    entry.marker,
                    header_depfile,
                    reflgen_dep_target(entry)
                )
            )
        end, {
            files = depfiles_with_reflgen(
                parse_reflgen_depfile(header_depfile, {entry.header})
            ),
            values = {
                "fei.reflect.file.v5",
                entry.output_file,
                entry.function_name,
                entry.header,
                table.concat(inputs.include_dirs, ";")
            },
            dependfile = dependfile
        })
    end
end

local function generate_module(target)
    if not reflected_target_enabled(target) then
        return
    end

    local inputs = module_inputs(target)
    validate_reflect_dependencies(target, inputs)
    if not inputs.module_file or not inputs.module_function then
        return
    end

    local functions = {}
    local depfiles = {}
    for _, entry in ipairs(inputs.entries) do
        table.insert(functions, entry.function_name)
        insert_unique(depfiles, entry.output_file)
    end
    for _, file in ipairs(reflection_runtime_headers()) do
        insert_unique(depfiles, file)
    end
    insert_unique(depfiles, path.join(os.projectdir(), "tools/reflgen/rules.lua"))

    local dependfile = project_absolute_path(inputs.module_file .. ".d")
    ensure_directory(path.directory(dependfile))

    depend.on_changed(function ()
        write_module_aggregate(inputs.module_file, inputs.module_function, functions)
        write_stamp_file(inputs.module_marker_file)
    end, {
        files = depfiles,
        values = {
            "fei.reflect.module.v5",
            inputs.module_file,
            inputs.module_function,
            table.concat(functions, ";")
        },
        dependfile = dependfile
    })
end

local function generate_aggregate(target)
    local output_file = target:values("fei.reflect.aggregate_file")
    if not output_file then
        return
    end

    local _, functions, files = aggregate_inputs(target)

    local dependfile = project_absolute_path(output_file .. ".d")
    ensure_directory(path.directory(dependfile))

    depend.on_changed(function ()
        write_aggregate(output_file, functions)
        write_stamp_file(target:values("fei.reflect.aggregate_marker_file"))
    end, {
        files = files,
        values = {
            "fei.reflect.aggregate.v5",
            output_file,
            table.concat(functions, ";")
        },
        dependfile = dependfile
    })
end

function buildcmd_file(target, batchcmds, sourcefile, opt)
    if not reflected_target_enabled(target) then
        return
    end

    local entry = entry_for_marker(target, sourcefile)
    if not entry then
        return
    end

    local inputs = module_inputs(target)
    validate_reflect_dependencies(target, inputs)
    cleanup_legacy_runner_files(target)
    local objectfile = target:objectfile(entry.output_file)
    local header_depfile = reflgen_depfile(entry)
    table.insert(target:objectfiles(), objectfile)

    batchcmds:show_progress(
        opt.progress,
        "${color.build.object}generating.reflgen %s",
        entry.header_relative
    )
    batchcmds:mkdir(path.directory(entry.output_file))
    batchcmds:vrunv(
        reflgen_program(),
        make_reflgen_args(
            {entry.header},
            inputs.include_dirs,
            entry.output_file,
            entry.function_name,
            entry.marker,
            header_depfile,
            reflgen_dep_target(entry)
        )
    )
    batchcmds:compile(entry.output_file, objectfile)
    batchcmds:add_depfiles(
        depfiles_with_reflgen(
            parse_reflgen_depfile(header_depfile, {entry.header})
        )
    )
    batchcmds:add_depvalues(
        "fei.reflect.file.v5",
        entry.output_file,
        entry.function_name,
        entry.header,
        table.concat(inputs.include_dirs, ";")
    )
    batchcmds:set_depmtime(os.mtime(objectfile))
    batchcmds:set_depcache(project_absolute_path(entry.output_file .. ".d"))
end

function buildcmd_module(target, batchcmds, opt)
    if not reflected_target_enabled(target) then
        return
    end

    local inputs = module_inputs(target)
    validate_reflect_dependencies(target, inputs)
    if not inputs.module_file or not inputs.module_function then
        return
    end

    local functions = {}
    local depfiles = {}
    for _, entry in ipairs(inputs.entries) do
        table.insert(functions, entry.function_name)
        insert_unique(depfiles, entry.output_file)
    end
    for _, file in ipairs(reflection_runtime_headers()) do
        insert_unique(depfiles, file)
    end
    insert_unique(depfiles, path.join(os.projectdir(), "tools/reflgen/rules.lua"))

    local objectfile = target:objectfile(inputs.module_file)
    table.insert(target:objectfiles(), objectfile)

    batchcmds:show_progress(
        opt.progress,
        "${color.build.object}generating.reflgen.module %s",
        target:name()
    )
    batchcmds:mkdir(path.directory(inputs.module_file))
    write_module_aggregate(inputs.module_file, inputs.module_function, functions)
    write_stamp_file(inputs.module_marker_file)
    batchcmds:compile(inputs.module_file, objectfile)
    batchcmds:add_depfiles(depfiles)
    batchcmds:add_depvalues(
        "fei.reflect.module.v5",
        inputs.module_file,
        inputs.module_function,
        table.concat(functions, ";")
    )
    batchcmds:set_depmtime(os.mtime(objectfile))
    batchcmds:set_depcache(project_absolute_path(inputs.module_file .. ".d"))
end

function buildcmd_aggregate(target, batchcmds, opt)
    local output_file, functions, files = aggregate_inputs(target)
    if not output_file then
        return
    end

    local objectfile = target:objectfile(output_file)
    table.insert(target:objectfiles(), objectfile)

    batchcmds:show_progress(
        opt.progress,
        "${color.build.object}generating.reflgen.aggregate %s",
        target:name()
    )
    batchcmds:mkdir(path.directory(output_file))
    write_aggregate(output_file, functions)
    write_stamp_file(target:values("fei.reflect.aggregate_marker_file"))
    batchcmds:compile(output_file, objectfile)
    batchcmds:add_depfiles(files)
    batchcmds:add_depvalues(
        "fei.reflect.aggregate.v5",
        output_file,
        table.concat(functions, ";")
    )
    batchcmds:set_depmtime(os.mtime(objectfile))
    batchcmds:set_depcache(project_absolute_path(output_file .. ".d"))
end

function generate(target)
    generate_files(target)
    generate_module(target)
    generate_aggregate(target)
end

function generate_all()
    for _, target in ipairs(project.ordertargets()) do
        generate(target)
    end
end
