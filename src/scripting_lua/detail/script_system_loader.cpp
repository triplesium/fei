#include "scripting_lua/detail/script_system_loader.hpp"

#include "ecs/dynamic/commands.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/resource.hpp"
#include "ecs/dynamic/system.hpp"
#include "ecs/system_config.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei::detail {
namespace {

Optional<TypeId> primitive_lua_type_id(std::string_view name) {
    auto& registry = Registry::instance();
    if (name == "bool") {
        return registry.register_type<bool>().id();
    }
    if (name == "i32") {
        return registry.register_type<int>().id();
    }
    if (name == "u32") {
        return registry.register_type<unsigned int>().id();
    }
    if (name == "f32") {
        return registry.register_type<float>().id();
    }
    if (name == "f64") {
        return registry.register_type<double>().id();
    }
    if (name == "string") {
        return registry.register_type<std::string>().id();
    }
    if (name == "entity") {
        return registry.register_type<Entity>().id();
    }
    return nullopt;
}

Result<TypeId, LuaScriptError> resolve_lua_script_type_ref(
    const LuaScriptTypeRef& type_ref,
    const std::unordered_map<std::string, TypeId>& script_types
) {
    if (type_ref.id) {
        return *type_ref.id;
    }
    if (auto primitive = primitive_lua_type_id(type_ref.name)) {
        return *primitive;
    }
    if (auto it = script_types.find(type_ref.name); it != script_types.end()) {
        return it->second;
    }

    auto type =
        Registry::instance().try_get_type(std::string_view {type_ref.name});
    if (type) {
        return type->id();
    }
    return failure(LuaScriptError {type.error().message});
}

Result<TypeId, LuaScriptError>
resolve_lua_script_system_type(const LuaScriptTypeRef& type_ref) {
    static const std::unordered_map<std::string, TypeId> no_script_types;
    return resolve_lua_script_type_ref(type_ref, no_script_types);
}

Status<LuaScriptError> append_lua_script_type_decl(
    const LuaScriptTypeDecl& type_decl,
    const std::unordered_map<std::string, const LuaScriptTypeDecl*>& type_decls,
    std::unordered_map<std::string, int>& visit_state,
    std::vector<const LuaScriptTypeDecl*>& ordered
) {
    auto& state = visit_state[type_decl.qualified_name];
    if (state == 2) {
        return {};
    }
    if (state == 1) {
        return failure(
            LuaScriptError {
                "Recursive script-defined type layout is not supported: " +
                type_decl.qualified_name
            }
        );
    }

    state = 1;
    for (const auto& field : type_decl.fields) {
        if (!field.type.script_type) {
            continue;
        }
        auto it = type_decls.find(field.type.name);
        if (it == type_decls.end()) {
            continue;
        }
        auto status = append_lua_script_type_decl(
            *it->second,
            type_decls,
            visit_state,
            ordered
        );
        if (!status) {
            return failure(std::move(status.error()));
        }
    }

    state = 2;
    ordered.push_back(&type_decl);
    return {};
}

Result<std::vector<const LuaScriptTypeDecl*>, LuaScriptError>
order_lua_script_type_decls(const LuaScriptModuleDecl& decl) {
    std::unordered_map<std::string, const LuaScriptTypeDecl*> type_decls;
    type_decls.reserve(decl.types.size());
    for (const auto& type_decl : decl.types) {
        if (!type_decls.emplace(type_decl.qualified_name, &type_decl).second) {
            return failure(
                LuaScriptError {
                    "Duplicate script-defined type '" +
                    type_decl.qualified_name + "'"
                }
            );
        }
    }

    std::unordered_map<std::string, int> visit_state;
    std::vector<const LuaScriptTypeDecl*> ordered;
    ordered.reserve(decl.types.size());
    for (const auto& type_decl : decl.types) {
        auto status = append_lua_script_type_decl(
            type_decl,
            type_decls,
            visit_state,
            ordered
        );
        if (!status) {
            return failure(std::move(status.error()));
        }
    }
    return ordered;
}

Status<LuaScriptError> register_lua_script_types(
    LuaRuntime& runtime,
    LuaScriptModuleId module,
    const LuaScriptModuleDecl& decl
) {
    auto ordered = order_lua_script_type_decls(decl);
    if (!ordered) {
        return failure(std::move(ordered.error()));
    }

    std::unordered_map<std::string, TypeId> script_types;
    script_types.reserve(ordered->size());
    auto& registry = Registry::instance();
    for (const auto* type_decl : *ordered) {
        std::vector<DynamicFieldDesc> fields;
        fields.reserve(type_decl->fields.size());
        for (const auto& field : type_decl->fields) {
            auto field_type =
                resolve_lua_script_type_ref(field.type, script_types);
            if (!field_type) {
                return failure(std::move(field_type.error()));
            }

            Optional<Val> default_value;
            if (field.has_default) {
                default_value = field.default_value;
            }
            fields.push_back(
                DynamicFieldDesc {
                    .name = field.name,
                    .type = *field_type,
                    .default_value = std::move(default_value),
                }
            );
        }

        auto registered = registry.register_dynamic_struct(
            DynamicStructDesc {
                .name = type_decl->qualified_name,
                .id = TypeId {type_decl->qualified_name},
                .fields = std::move(fields),
            }
        );
        if (!registered) {
            return failure(
                LuaScriptError {std::move(registered.error().message)}
            );
        }

        script_types.emplace(type_decl->qualified_name, registered->id());
        auto bound =
            runtime.bind_module_type(module, type_decl->name, *registered);
        if (!bound) {
            return failure(std::move(bound.error()));
        }
    }

    return {};
}

Status<LuaScriptError> apply_lua_script_resource_initial_values(
    Val& value,
    const LuaScriptResourceDecl& resource
) {
    if (resource.initial_values.empty()) {
        return {};
    }

    auto cls = Registry::instance().try_get_cls(value.type_id());
    if (!cls) {
        return failure(LuaScriptError {std::move(cls.error().message)});
    }

    for (const auto& field : resource.initial_values) {
        auto property = cls->try_get_property(field.name);
        if (!property) {
            return failure(
                LuaScriptError {std::move(property.error().message)}
            );
        }
        if (!field.value) {
            return failure(
                LuaScriptError {
                    "Resource initial value for field '" + field.name +
                    "' is unsupported"
                }
            );
        }

        auto assigned = property->set(value.ref(), field.value.ref());
        if (!assigned) {
            return failure(
                LuaScriptError {std::move(assigned.error().message)}
            );
        }
    }

    return {};
}

Status<LuaScriptError>
install_lua_script_resources(World& world, const LuaScriptModuleDecl& decl) {
    auto& registry = Registry::instance();
    for (const auto& resource : decl.resources) {
        auto type = registry.try_get_type(std::string_view {resource.type});
        if (!type) {
            return failure(LuaScriptError {std::move(type.error().message)});
        }
        if (resource.init_if_missing && world.has_resource(type->id())) {
            continue;
        }
        if (!type->default_constructible()) {
            return failure(
                LuaScriptError {
                    "Resource type '" + type->name() +
                    "' is not default constructible"
                }
            );
        }

        auto value = Val::default_construct(*type);
        auto initialized =
            apply_lua_script_resource_initial_values(value, resource);
        if (!initialized) {
            return failure(std::move(initialized.error()));
        }

        world.add_resource(type->id(), std::move(value));
    }

    return {};
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
    auto script_types = register_lua_script_types(runtime, module, decl);
    if (!script_types) {
        return failure(std::move(script_types.error()));
    }

    auto script_resources = install_lua_script_resources(world, decl);
    if (!script_resources) {
        return failure(std::move(script_resources.error()));
    }

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
