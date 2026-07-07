import("core.project.project")
import("lib.detect.find_tool")

local schema_version = "fei.shader.v4"
local spirv_target_env = "vulkan1.1"

local shader_stage_extensions = {
    vertex = ".vert",
    geometry = ".geom",
    fragment = ".frag",
    compute = ".comp",
}

local shader_stage_entries = {
    vertex = "vertex_main",
    geometry = "geometry_main",
    fragment = "fragment_main",
    compute = "compute_main",
}

local shader_stage_order = {"vertex", "geometry", "fragment", "compute"}

local function normalize_path(filepath)
    return path.unix(tostring(filepath))
end

local function project_absolute_path(filepath)
    filepath = normalize_path(filepath)
    if path.is_absolute(filepath) then
        return filepath
    end
    return normalize_path(path.join(os.projectdir(), filepath))
end

local function configured_tool(override)
    if type(override) == "string" and #override > 0 then
        return override
    end
    if type(override) == "table" and #override > 0 then
        return override[1]
    end
    return nil
end

local function find_vulkan_sdk_tool(name)
    local vulkan_sdk = os.getenv("VULKAN_SDK")
    if not vulkan_sdk or #vulkan_sdk == 0 then
        return nil
    end

    for _, filename in ipairs({name, name .. ".exe"}) do
        local program = path.join(vulkan_sdk, "Bin", filename)
        if os.isfile(program) then
            return program
        end
    end
    return nil
end

local function find_shader_tool(name, override)
    local configured = configured_tool(override)
    if configured then
        return configured
    end

    local tool = find_tool(name)
    if tool then
        return tool.program
    end
    return find_vulkan_sdk_tool(name)
end

local function find_glslc(override)
    local glslc = find_shader_tool("glslc", override)
    if glslc then
        return glslc
    end

    raise("glslc not found in PATH or VULKAN_SDK! Install glslc or set fei.shader.glslc.")
end

local function find_slangc(override)
    local slangc = find_shader_tool("slangc", override)
    if slangc then
        return slangc
    end

    raise("slangc not found in PATH or VULKAN_SDK! Install slangc or set fei.shader.slangc.")
end

local function is_slang_source(sourcefile)
    return path.extension(sourcefile) == ".slang"
end

local function strip_slang_suffix(relpath)
    local suffix = ".slang"
    if relpath:sub(-#suffix) == suffix then
        return relpath:sub(1, #relpath - #suffix)
    end
    return relpath
end

local function slang_stage_from_logical_path(logical_relpath)
    local extension = path.extension(logical_relpath)
    if extension == ".vert" then
        return "vertex"
    end
    if extension == ".geom" then
        return "geometry"
    end
    if extension == ".frag" then
        return "fragment"
    end
    if extension == ".comp" then
        return "compute"
    end
    return nil
end

local function make_output_paths(source_root, output_root, sourcefile, job)
    local relpath = normalize_path(path.relative(sourcefile, source_root))
    local logical_relpath = job.logical_relpath or strip_slang_suffix(relpath)
    local dep_relpath = job.dep_relpath or relpath
    return {
        relpath = relpath,
        logical_relpath = logical_relpath,
        stage = job.stage,
        entry = job.entry or "main",
        spirv = path.join(output_root, "vulkan", logical_relpath .. ".spv"),
        opengl = path.join(output_root, "opengl", logical_relpath),
        reflection = path.join(output_root, "reflection", logical_relpath .. ".json"),
        slang_reflection = path.join(output_root, "reflection", dep_relpath .. ".json"),
        make_depfile = path.join(output_root, "deps", dep_relpath .. ".mk.d"),
        xmake_depcache = path.join(output_root, "deps", relpath .. ".xmake.d"),
    }
end

local function shader_stage_attribute_pattern(stage)
    return "%[shader%s*%(%s*\"" .. stage .. "\"%s*%)%s*%]"
end

local function make_multi_entry_slang_jobs(source_root, output_root, sourcefile, base_logical_relpath)
    local content = io.readfile(sourcefile)
    if not content then
        raise("failed to read Slang shader source " .. sourcefile)
    end

    local relpath = normalize_path(path.relative(sourcefile, source_root))
    local jobs = {}
    for _, stage in ipairs(shader_stage_order) do
        if content:find(shader_stage_attribute_pattern(stage)) then
            local entry = shader_stage_entries[stage]
            if not content:find(entry, 1, true) then
                raise(
                    string.format(
                        "multi-entry Slang shader %s has [shader(\"%s\")] but no %s entry",
                        relpath,
                        stage,
                        entry
                    )
                )
            end
            table.insert(
                jobs,
                make_output_paths(source_root, output_root, sourcefile, {
                    logical_relpath = base_logical_relpath .. shader_stage_extensions[stage],
                    stage = stage,
                    entry = entry,
                    dep_relpath = relpath .. "." .. stage,
                })
            )
        end
    end

    if #jobs == 0 then
        raise("Slang shader " .. relpath .. " has no stage suffix and no [shader(...)] entries")
    end
    return jobs
end

local function make_shader_jobs(source_root, output_root, sourcefile)
    local relpath = normalize_path(path.relative(sourcefile, source_root))
    local logical_relpath = strip_slang_suffix(relpath)
    local stage = slang_stage_from_logical_path(logical_relpath)
    if stage then
        return {
            make_output_paths(source_root, output_root, sourcefile, {
                logical_relpath = logical_relpath,
                stage = stage,
                entry = "main",
            }),
        }
    end

    if is_slang_source(sourcefile) then
        return make_multi_entry_slang_jobs(source_root, output_root, sourcefile, logical_relpath)
    end

    raise("unknown shader stage for " .. logical_relpath)
end

local function required_outputs(outputs, options)
    options = options or {}
    local files = {outputs.spirv}
    if not options.spirv_only then
        table.insert(files, outputs.opengl)
    end
    if not options.no_reflect then
        table.insert(files, outputs.reflection)
    end
    if outputs.slang_reflection and options.slang then
        table.insert(files, outputs.slang_reflection)
    end
    return files
end

local function minimum_mtime(files)
    local value = nil
    for _, file in ipairs(files) do
        if not os.isfile(file) then
            return 0
        end
        local file_mtime = os.mtime(file) or 0
        if not value or file_mtime < value then
            value = file_mtime
        end
    end
    return value or 0
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

local function parse_make_depfile(depfile)
    local depfiles = {}
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
        token = token:gsub("\\ ", " ")
        token = token:gsub("\\\\", "\\")
        if #token > 0 and os.isfile(token) then
            insert_unique(depfiles, token)
        end
    end
    return depfiles
end

local function shadergen_program()
    local target = assert(project.target("fei-shadergen"), "target fei-shadergen not found")
    return target:targetfile()
end

local function helper_depfiles(shadergen)
    local files = {}
    insert_unique(files, path.join(os.projectdir(), "tools/shadergen/rules.lua"))
    for _, file in ipairs(os.files(path.join(os.projectdir(), "tools/shadergen/*.cpp"))) do
        insert_unique(files, file)
    end
    for _, file in ipairs(os.files(path.join(os.projectdir(), "tools/shadergen/*.hpp"))) do
        insert_unique(files, file)
    end
    insert_unique(files, shadergen)
    return files
end

local function shader_header_depfiles(source_root)
    local files = {}
    for _, file in ipairs(os.files(path.join(source_root, "**.slangh"))) do
        insert_unique(files, file)
    end
    return files
end

local function shader_depfiles(source_root, sourcefile, make_depfile, shadergen)
    local depfiles = {}
    insert_unique(depfiles, sourcefile)
    for _, file in ipairs(parse_make_depfile(make_depfile)) do
        insert_unique(depfiles, file)
    end
    for _, file in ipairs(shader_header_depfiles(source_root)) do
        insert_unique(depfiles, file)
    end
    for _, file in ipairs(helper_depfiles(shadergen)) do
        insert_unique(depfiles, file)
    end
    return depfiles
end

local function source_root_for_target(target)
    local configured = target and target:values("fei.shader.source_root")
    if configured and #configured > 0 then
        return project_absolute_path(configured)
    end
    if target then
        return project_absolute_path(path.join(target:scriptdir(), "shaders"))
    end
    return project_absolute_path(path.join("src", "pbr", "shaders"))
end

local function output_root_for_target(target)
    local configured = target and target:values("fei.shader.output_root")
    if configured and #configured > 0 then
        return project_absolute_path(configured)
    end
    return project_absolute_path(path.join("build", "generated", "shaders"))
end

function buildcmd_file(target, batchcmds, sourcefile, opt)
    local source_root = source_root_for_target(target)
    local output_root = output_root_for_target(target)
    sourcefile = normalize_path(sourcefile)
    local jobs = make_shader_jobs(source_root, output_root, sourcefile)
    local slang = is_slang_source(sourcefile)
    local compiler = slang and find_slangc(target:values("fei.shader.slangc")) or find_glslc(target:values("fei.shader.glslc"))
    local shadergen = shadergen_program()

    batchcmds:show_progress(
        opt.progress,
        "${color.build.object}shader %s",
        jobs[1].relpath
    )

    local depvalues = {
        schema_version,
        compiler,
        shadergen,
        spirv_target_env,
        source_root,
        output_root,
        slang,
    }
    local required = {}

    for _, outputs in ipairs(jobs) do
        batchcmds:mkdir(path.directory(outputs.spirv))
        batchcmds:mkdir(path.directory(outputs.opengl))
        batchcmds:mkdir(path.directory(outputs.reflection))
        batchcmds:mkdir(path.directory(outputs.make_depfile))
        batchcmds:vrunv(
            compiler,
            slang and {
                "-target",
                "spirv",
                "-profile",
                "glsl_450",
                "-matrix-layout-row-major",
                "-entry",
                outputs.entry,
                "-stage",
                outputs.stage,
                "-I",
                source_root,
                "-depfile",
                outputs.make_depfile,
                "-reflection-json",
                outputs.slang_reflection,
                "-o",
                outputs.spirv,
                sourcefile,
            } or {
                "--target-env=" .. spirv_target_env,
                "-I",
                source_root,
                "-MD",
                "-MF",
                outputs.make_depfile,
                "-MT",
                outputs.relpath .. ".spv",
                "-o",
                outputs.spirv,
                sourcefile,
            }
        )
        local shadergen_args = {
            "--input",
            outputs.spirv,
            "--glsl",
            outputs.opengl,
            "--reflect",
            outputs.reflection,
        }
        if slang then
            table.insert(shadergen_args, "--slang-reflect")
            table.insert(shadergen_args, outputs.slang_reflection)
        end
        batchcmds:vrunv(
            shadergen,
            shadergen_args
        )
        batchcmds:add_depfiles(shader_depfiles(source_root, sourcefile, outputs.make_depfile, shadergen))
        for _, required_output in ipairs(required_outputs(outputs, {slang = slang})) do
            table.insert(required, required_output)
        end
        table.insert(depvalues, outputs.relpath)
        table.insert(depvalues, outputs.logical_relpath)
        table.insert(depvalues, outputs.stage)
        table.insert(depvalues, outputs.entry)
    end

    batchcmds:add_depvalues(table.unpack(depvalues))
    batchcmds:set_depmtime(minimum_mtime(required))
    batchcmds:set_depcache(project_absolute_path(jobs[1].xmake_depcache))
end
