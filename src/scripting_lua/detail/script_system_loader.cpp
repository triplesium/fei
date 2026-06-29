#include "scripting_lua/detail/script_system_loader.hpp"

#include "ecs/dynamic/commands.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/resource.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/system_config.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace fei::detail {
namespace {

Result<TypeId, LuaScriptError>
resolve_lua_script_system_type(const LuaScriptTypeRef& type_ref) {
    if (type_ref.script_type) {
        return failure(
            LuaScriptError {
                "script-defined type storage is not implemented: " +
                type_ref.name
            }
        );
    }
    if (type_ref.id) {
        return *type_ref.id;
    }

    auto type =
        Registry::instance().try_get_type(std::string_view {type_ref.name});
    if (type) {
        return type->id();
    }
    return failure(LuaScriptError {type.error().message});
}

Result<DynamicSystemParamPtr, LuaScriptError>
compile_lua_script_system_param(const LuaScriptSystemParam& param) {
    if (param.kind == LuaScriptSystemParamKind::Commands) {
        if (param.name.empty()) {
            return failure(
                LuaScriptError {"Commands script system param missing name"}
            );
        }

        DynamicSystemParamPtr result =
            std::make_unique<DynamicCommandsParam>(param.name);
        return std::move(result);
    }

    if (param.kind == LuaScriptSystemParamKind::Query) {
        if (param.name.empty()) {
            return failure(
                LuaScriptError {"Query script system param missing name"}
            );
        }

        std::vector<DynamicQueryField> fields;
        std::vector<DynamicQueryFilter> filters;
        for (const auto& child : param.query_params) {
            if (child.kind == LuaScriptQueryParamKind::Entity) {
                if (child.name.empty()) {
                    return failure(
                        LuaScriptError {
                            "Query entity script system param missing name"
                        }
                    );
                }
                fields.push_back(
                    DynamicQueryField {
                        .name = child.name,
                        .type = type_id<Entity>(),
                        .access = DynamicParamAccess::Read,
                        .kind = DynamicQueryFieldKind::Entity,
                    }
                );
                continue;
            }

            if (child.type.name.empty()) {
                return failure(
                    LuaScriptError {"Query script system param missing type"}
                );
            }

            auto type = resolve_lua_script_system_type(child.type);
            if (!type) {
                return failure(std::move(type.error()));
            }

            if (child.kind == LuaScriptQueryParamKind::Component) {
                if (child.name.empty()) {
                    return failure(
                        LuaScriptError {
                            "Query component script system param missing name"
                        }
                    );
                }
                fields.push_back(
                    DynamicQueryField {
                        .name = child.name,
                        .type = *type,
                        .access = child.access,
                    }
                );
            } else {
                filters.push_back(
                    DynamicQueryFilter {
                        .type = *type,
                        .required = child.kind == LuaScriptQueryParamKind::With,
                    }
                );
            }
        }

        if (fields.empty()) {
            return failure(
                LuaScriptError {"Query script system param must declare fields"}
            );
        }

        DynamicSystemParamPtr result = std::make_unique<DynamicQuery>(
            param.name,
            std::move(fields),
            std::move(filters)
        );
        return std::move(result);
    }

    if (param.kind != LuaScriptSystemParamKind::Resource) {
        return failure(
            LuaScriptError {
                "Only resource, query, and commands script system params are "
                "supported yet"
            }
        );
    }
    if (param.type.name.empty()) {
        return failure(
            LuaScriptError {"Resource script system param missing type"}
        );
    }

    auto type = resolve_lua_script_system_type(param.type);
    if (!type) {
        return failure(std::move(type.error()));
    }

    DynamicSystemParamPtr result = std::make_unique<DynamicResourceParam>(
        param.name,
        *type,
        param.access,
        param.optional
    );
    return std::move(result);
}

Result<DynamicSystemParams, LuaScriptError>
compile_lua_script_system_params(const LuaScriptSystemDecl& decl) {
    DynamicSystemParams params;
    params.reserve(decl.params.size());
    for (const auto& param : decl.params) {
        auto compiled = compile_lua_script_system_param(param);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        params.push_back(std::move(*compiled));
    }
    return params;
}

class LuaScriptSystemExecutor final : public DynamicSystemExecutor {
  private:
    LuaRuntime* m_runtime {nullptr};
    LuaScriptModuleId m_module {invalid_lua_script_module_id};
    std::string m_name;

  public:
    LuaScriptSystemExecutor(
        LuaRuntime& runtime,
        LuaScriptModuleId module,
        std::string name
    ) : m_runtime(&runtime), m_module(module), m_name(std::move(name)) {}

    Status<DynamicSystemError> execute(const std::vector<Ref>& args) override {
        auto status = m_runtime->call_module_function(m_module, m_name, args);
        if (!status) {
            return failure(
                DynamicSystemError {std::move(status.error().message)}
            );
        }
        return {};
    }
};

} // namespace

Result<SystemAccess, LuaScriptError>
lua_script_system_access_for_decl(const LuaScriptSystemDecl& decl) {
    auto params = compile_lua_script_system_params(decl);
    if (!params) {
        return failure(std::move(params.error()));
    }
    return dynamic_system_access_for_params(*params);
}

SystemProfileInfo lua_script_system_profile_for_decl(
    const LuaScriptModuleDecl& module_decl,
    const LuaScriptSystemDecl& system_decl
) {
    const auto file = module_decl.source_name.empty() ?
                          std::string {"<script>"} :
                          module_decl.source_name;
    return SystemProfileInfo {
        .name = file + "::" + system_decl.name,
        .file = file,
        .function = system_decl.name,
        .line = 0,
    };
}

Result<std::vector<SystemHandle>, LuaScriptError> install_lua_script_systems(
    World& world,
    LuaRuntime& runtime,
    LuaScriptModuleId module,
    const LuaScriptModuleDecl& decl
) {
    struct CompiledLuaScriptSystem {
        const LuaScriptSystemDecl* decl {nullptr};
        DynamicSystemParams params;
        std::unique_ptr<DynamicSystemExecutor> executor;
    };

    std::vector<CompiledLuaScriptSystem> compiled_systems;
    compiled_systems.reserve(decl.systems.size());
    for (const auto& system : decl.systems) {
        if (system.name.empty()) {
            return failure(LuaScriptError {"Script system missing name"});
        }
        auto params = compile_lua_script_system_params(system);
        if (!params) {
            return failure(std::move(params.error()));
        }
        compiled_systems.push_back(
            CompiledLuaScriptSystem {
                .decl = &system,
                .params = std::move(*params),
                .executor = std::make_unique<LuaScriptSystemExecutor>(
                    runtime,
                    module,
                    system.name
                ),
            }
        );
    }

    std::vector<SystemHandle> handles;
    handles.reserve(compiled_systems.size());
    for (auto& system : compiled_systems) {
        auto dynamic_system = std::make_unique<DynamicSystem>(
            system.decl->name,
            std::move(system.params),
            std::move(system.executor)
        );
        SystemConfig config(std::move(dynamic_system));
        config.profile = lua_script_system_profile_for_decl(decl, *system.decl);
        handles.push_back(
            world.add_system(system.decl->schedule, std::move(config))
        );
    }
    return handles;
}

} // namespace fei::detail
