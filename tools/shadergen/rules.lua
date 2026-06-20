import("core.project.project")
import("lib.detect.find_tool")

local schema_version = "fei.shader.v2"
local spirv_target_env = "vulkan1.1"

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

local function find_glslc(override)
    if override and #override > 0 then
        return override
    end
    local glslc = find_tool("glslc")
    if not glslc then
        raise("glslc not found in PATH! Install glslc or set fei.shader.glslc.")
    end
    return glslc.program
end

local function make_output_paths(source_root, output_root, sourcefile)
    local relpath = normalize_path(path.relative(sourcefile, source_root))
    return {
        relpath = relpath,
        spirv = path.join(output_root, "vulkan", relpath .. ".spv"),
        opengl = path.join(output_root, "opengl", relpath),
        reflection = path.join(output_root, "reflection", relpath .. ".json"),
        make_depfile = path.join(output_root, "deps", relpath .. ".mk.d"),
        xmake_depcache = path.join(output_root, "deps", relpath .. ".xmake.d"),
    }
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

local function shader_depfiles(sourcefile, make_depfile, shadergen)
    local depfiles = {}
    insert_unique(depfiles, sourcefile)
    for _, file in ipairs(parse_make_depfile(make_depfile)) do
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
    local outputs = make_output_paths(source_root, output_root, normalize_path(sourcefile))
    local glslc = find_glslc(target:values("fei.shader.glslc"))
    local shadergen = shadergen_program()

    batchcmds:show_progress(
        opt.progress,
        "${color.build.object}shader %s",
        outputs.relpath
    )
    batchcmds:mkdir(path.directory(outputs.spirv))
    batchcmds:mkdir(path.directory(outputs.opengl))
    batchcmds:mkdir(path.directory(outputs.reflection))
    batchcmds:mkdir(path.directory(outputs.make_depfile))
    batchcmds:vrunv(
        glslc,
        {
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
    batchcmds:vrunv(
        shadergen,
        {
            "--input",
            outputs.spirv,
            "--glsl",
            outputs.opengl,
            "--reflect",
            outputs.reflection,
        }
    )
    batchcmds:add_depfiles(shader_depfiles(sourcefile, outputs.make_depfile, shadergen))
    batchcmds:add_depvalues(
        schema_version,
        glslc,
        shadergen,
        spirv_target_env,
        source_root,
        output_root,
        outputs.relpath
    )
    batchcmds:set_depmtime(minimum_mtime(required_outputs(outputs)))
    batchcmds:set_depcache(project_absolute_path(outputs.xmake_depcache))
end
