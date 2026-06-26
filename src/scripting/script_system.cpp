#include "scripting/script_system.hpp"

#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <memory>
#include <string_view>
#include <utility>

namespace fei {
namespace {

void add_script_arg_access(SystemAccess& access, const ScriptSystemArg& arg) {
    if (arg.kind == ScriptSystemParamKind::Query) {
        for (const auto& field : arg.query_fields) {
            if (field.access == ScriptSystemAccess::Write) {
                access.write_components.insert(field.type);
            } else {
                access.read_components.insert(field.type);
            }
        }
        return;
    }

    if (arg.kind != ScriptSystemParamKind::Resource) {
        return;
    }

    if (arg.access == ScriptSystemAccess::Write) {
        access.write_resources.insert(arg.type);
    } else {
        access.read_resources.insert(arg.type);
    }
}

Result<ScriptSystemArg, ScriptError>
compile_script_system_arg(const ScriptSystemParam& param) {
    if (param.kind == ScriptSystemParamKind::Query) {
        if (param.name.empty()) {
            return failure(
                ScriptError {"Query script system param missing name"}
            );
        }

        std::vector<ScriptQueryField> fields;
        std::vector<ScriptQueryFilter> filters;
        for (const auto& child : param.params) {
            if (child.kind != ScriptSystemParamKind::Component &&
                child.kind != ScriptSystemParamKind::With &&
                child.kind != ScriptSystemParamKind::Without) {
                return failure(
                    ScriptError {"Query script system params only support "
                                 "components and "
                                 "filters"}
                );
            }
            if (child.type.empty()) {
                return failure(
                    ScriptError {"Query script system param missing type"}
                );
            }

            auto type = Registry::instance().try_get_type(
                std::string_view {child.type}
            );
            if (!type) {
                return failure(ScriptError {type.error().message});
            }

            if (child.kind == ScriptSystemParamKind::Component) {
                if (child.name.empty()) {
                    return failure(
                        ScriptError {
                            "Query component script system param missing name"
                        }
                    );
                }
                fields.push_back(
                    ScriptQueryField {
                        .name = child.name,
                        .type = type->id(),
                        .access = child.access,
                    }
                );
            } else {
                filters.push_back(
                    ScriptQueryFilter {
                        .type = type->id(),
                        .required = child.kind == ScriptSystemParamKind::With,
                    }
                );
            }
        }

        if (fields.empty()) {
            return failure(
                ScriptError {
                    "Query script system param must declare components"
                }
            );
        }

        return ScriptSystemArg {
            .kind = param.kind,
            .access = param.access,
            .type = {},
            .name = param.name,
            .query_fields = std::move(fields),
            .query_filters = std::move(filters),
        };
    }

    if (param.kind != ScriptSystemParamKind::Resource) {
        return failure(
            ScriptError {
                "Only resource and query script system params are supported yet"
            }
        );
    }
    if (param.type.empty()) {
        return failure(
            ScriptError {"Resource script system param missing type"}
        );
    }

    auto type =
        Registry::instance().try_get_type(std::string_view {param.type});
    if (!type) {
        return failure(ScriptError {type.error().message});
    }

    return ScriptSystemArg {
        .kind = param.kind,
        .access = param.access,
        .type = type->id(),
        .name = param.name,
    };
}

Result<std::vector<ScriptSystemArg>, ScriptError>
compile_script_system_args(const ScriptSystemManifest& manifest) {
    std::vector<ScriptSystemArg> args;
    args.reserve(manifest.params.size());
    for (const auto& param : manifest.params) {
        auto arg = compile_script_system_arg(param);
        if (!arg) {
            return failure(std::move(arg.error()));
        }
        args.push_back(std::move(*arg));
    }
    return args;
}

SystemAccess
script_system_access_for_args(const std::vector<ScriptSystemArg>& args) {
    SystemAccess access;
    for (const auto& arg : args) {
        add_script_arg_access(access, arg);
    }
    return access;
}

} // namespace

Result<SystemAccess, ScriptError>
script_system_access_for_manifest(const ScriptSystemManifest& manifest) {
    auto args = compile_script_system_args(manifest);
    if (!args) {
        return failure(std::move(args.error()));
    }
    return script_system_access_for_args(*args);
}

SystemProfileInfo script_system_profile_for_manifest(
    const ScriptModuleManifest& module_manifest,
    const ScriptSystemManifest& system_manifest
) {
    const auto file = module_manifest.source_name.empty() ?
                          std::string {"<script>"} :
                          module_manifest.source_name;
    return SystemProfileInfo {
        .name = file + "::" + system_manifest.name,
        .file = file,
        .function = system_manifest.name,
        .line = 0,
    };
}

ScriptSystem::ScriptSystem(
    ScriptRuntime& runtime,
    ScriptModuleId module,
    std::string name,
    std::vector<ScriptSystemArg> args,
    SystemAccess access
) :
    m_runtime(&runtime), m_module(module), m_name(std::move(name)),
    m_args(std::move(args)), m_access(std::move(access)) {}

void ScriptSystem::run(World& world) {
    std::vector<Ref> args;
    args.reserve(m_args.size());
    std::vector<ScriptQuery> queries;
    queries.reserve(m_args.size());
    for (const auto& arg : m_args) {
        if (arg.kind == ScriptSystemParamKind::Query) {
            queries.emplace_back(world, arg.query_fields, arg.query_filters);
            args.emplace_back(queries.back());
            continue;
        }

        if (arg.kind != ScriptSystemParamKind::Resource) {
            error(
                "Script system '{}' has unsupported param '{}'",
                m_name,
                arg.name
            );
            return;
        }
        if (!world.has_resource(arg.type)) {
            error(
                "Script system '{}' missing resource '{}'",
                m_name,
                type_name(arg.type)
            );
            return;
        }
        if (arg.access == ScriptSystemAccess::Write) {
            args.push_back(world.resource(arg.type));
        } else {
            args.push_back(static_cast<const World&>(world).resource(arg.type));
        }
    }

    auto status = m_runtime->call_module_function(m_module, m_name, args);
    if (!status) {
        error("Script system '{}' failed: {}", m_name, status.error().message);
    }
}

Result<std::vector<SystemHandle>, ScriptError> register_script_systems(
    World& world,
    ScriptRuntime& runtime,
    ScriptModuleId module,
    const ScriptModuleManifest& manifest
) {
    struct CompiledScriptSystem {
        const ScriptSystemManifest* manifest {nullptr};
        std::vector<ScriptSystemArg> args;
        SystemAccess access;
    };

    std::vector<CompiledScriptSystem> compiled_systems;
    compiled_systems.reserve(manifest.systems.size());
    for (const auto& system : manifest.systems) {
        if (system.name.empty()) {
            return failure(ScriptError {"Script system missing name"});
        }
        auto args = compile_script_system_args(system);
        if (!args) {
            return failure(std::move(args.error()));
        }
        auto access = script_system_access_for_args(*args);
        compiled_systems.push_back(
            CompiledScriptSystem {
                .manifest = &system,
                .args = std::move(*args),
                .access = std::move(access),
            }
        );
    }

    std::vector<SystemHandle> handles;
    handles.reserve(compiled_systems.size());
    for (auto& system : compiled_systems) {
        auto script_system = std::make_unique<ScriptSystem>(
            runtime,
            module,
            system.manifest->name,
            std::move(system.args),
            std::move(system.access)
        );
        SystemConfig config(std::move(script_system));
        config.profile =
            script_system_profile_for_manifest(manifest, *system.manifest);
        handles.push_back(
            world.add_system(system.manifest->schedule, std::move(config))
        );
    }
    return handles;
}

} // namespace fei
