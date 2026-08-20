#include "scripting/module_install.hpp"

#include "ecs/dynamic/system_decl.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"
#include "scripting/state.hpp"

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace fei {
namespace {

class CallbackScriptSystemExecutor final : public DynamicSystemExecutor {
  private:
    ScriptSystemCall m_call;
    bool m_checkpoint_safe_stateless {false};

  public:
    CallbackScriptSystemExecutor(
        ScriptSystemCall call,
        bool checkpoint_safe_stateless
    ) :
        m_call(std::move(call)),
        m_checkpoint_safe_stateless(checkpoint_safe_stateless) {}

    Status<DynamicSystemError> execute(const std::vector<Ref>& args) override {
        auto status = m_call(args);
        if (!status) {
            return failure(
                DynamicSystemError {std::move(status.error().message)}
            );
        }
        return {};
    }

    bool checkpoint_safe_stateless() const override {
        return m_checkpoint_safe_stateless;
    }
};

class CallbackScriptConditionExecutor final : public DynamicConditionExecutor {
  private:
    ScriptConditionCall m_call;
    bool m_checkpoint_safe_stateless {false};

  public:
    CallbackScriptConditionExecutor(
        ScriptConditionCall call,
        bool checkpoint_safe_stateless
    ) :
        m_call(std::move(call)),
        m_checkpoint_safe_stateless(checkpoint_safe_stateless) {}

    Result<bool, DynamicSystemError>
    evaluate(const std::vector<Ref>& args) override {
        auto result = m_call(args);
        if (!result) {
            return failure(
                DynamicSystemError {std::move(result.error().message)}
            );
        }
        return *result;
    }

    bool checkpoint_safe_stateless() const override {
        return m_checkpoint_safe_stateless;
    }
};

Result<TypeId, ScriptError> resolve_script_type_ref(
    const ScriptTypeRef& type_ref,
    const std::unordered_map<std::string, TypeId>& script_types
) {
    TypeId resolved;
    if (type_ref.type_id) {
        if (*type_ref.type_id == type_id<Entity>()) {
            resolved = Registry::instance().register_type<Entity>().id();
        } else {
            resolved = *type_ref.type_id;
        }
    } else if (
        auto it = script_types.find(type_ref.type_name);
        it != script_types.end()
    ) {
        resolved = it->second;
    } else {
        auto type = resolve_dynamic_type_ref(
            DynamicTypeRef {
                .type_name = type_ref.type_name,
            }
        );
        if (!type) {
            return failure(ScriptError {std::move(type.error().message)});
        }
        resolved = *type;
    }

    if (!type_ref.optional) {
        return resolved;
    }
    if (resolved == type_id<Entity>()) {
        return Registry::instance().register_type<Optional<Entity>>().id();
    }
    return failure(
        ScriptError {
            "Optional script fields currently support only Entity values",
        }
    );
}

Status<ScriptError> append_script_type_decl(
    const ScriptTypeDecl& type_decl,
    const std::unordered_map<std::string, const ScriptTypeDecl*>& type_decls,
    std::unordered_map<std::string, int>& visit_state,
    std::vector<const ScriptTypeDecl*>& ordered
) {
    auto& state = visit_state[type_decl.qualified_name];
    if (state == 2) {
        return {};
    }
    if (state == 1) {
        return failure(
            ScriptError {
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
        auto it = type_decls.find(field.type.type_name);
        if (it == type_decls.end()) {
            continue;
        }
        auto status = append_script_type_decl(
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

Result<std::vector<const ScriptTypeDecl*>, ScriptError>
order_script_type_decls(const ScriptModuleDecl& decl) {
    std::unordered_map<std::string, const ScriptTypeDecl*> type_decls;
    type_decls.reserve(decl.types.size());
    for (const auto& type_decl : decl.types) {
        if (!type_decls.emplace(type_decl.qualified_name, &type_decl).second) {
            return failure(
                ScriptError {
                    "Duplicate script-defined type '" +
                    type_decl.qualified_name + "'"
                }
            );
        }
    }

    std::unordered_map<std::string, int> visit_state;
    std::vector<const ScriptTypeDecl*> ordered;
    ordered.reserve(decl.types.size());
    for (const auto& type_decl : decl.types) {
        auto status = append_script_type_decl(
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

bool default_values_equal(const Optional<Val>& lhs, const Optional<Val>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs) {
        return true;
    }
    if (lhs->type_id() != rhs->type_id()) {
        return false;
    }
    auto equal =
        lhs->type()->equals(lhs->ref().const_ptr(), rhs->ref().const_ptr());
    return equal && *equal;
}

bool layouts_match(
    const DynamicStructLayout& layout,
    const DynamicStructDesc& desc
) {
    if (layout.name != desc.name || layout.id != desc.id ||
        layout.fields.size() != desc.fields.size()) {
        return false;
    }
    for (std::size_t i = 0; i < layout.fields.size(); ++i) {
        const auto& existing = layout.fields[i];
        const auto& requested = desc.fields[i];
        if (existing.name != requested.name ||
            existing.type != requested.type ||
            !default_values_equal(
                existing.default_value,
                requested.default_value
            )) {
            return false;
        }
    }
    return true;
}

Result<Type&, ScriptError>
ensure_dynamic_struct(Registry& registry, DynamicStructDesc desc) {
    if (auto existing = registry.try_get_type(desc.id)) {
        const auto* layout = registry.try_get_dynamic_struct_layout(desc.id);
        if (layout && layouts_match(*layout, desc)) {
            return *existing;
        }
        return failure(
            ScriptError {
                "Script-defined type schema conflicts with existing type '" +
                existing->name() + "'"
            }
        );
    }

    auto registered = registry.register_dynamic_struct(std::move(desc));
    if (!registered) {
        return failure(ScriptError {std::move(registered.error().message)});
    }
    return *registered;
}

Status<ScriptError> apply_script_resource_initial_values(
    Val& value,
    const ScriptResourceDecl& resource
) {
    if (resource.initial_values.empty()) {
        return {};
    }

    auto cls = Registry::instance().try_get_cls(value.type_id());
    if (!cls) {
        return failure(ScriptError {std::move(cls.error().message)});
    }

    for (const auto& field : resource.initial_values) {
        auto property = cls->try_get_property(field.name);
        if (!property) {
            return failure(ScriptError {std::move(property.error().message)});
        }
        if (!field.value) {
            return failure(
                ScriptError {
                    "Resource initial value for field '" + field.name +
                    "' is unsupported"
                }
            );
        }

        auto assigned = property->set(value.ref(), field.value.ref());
        if (!assigned) {
            return failure(ScriptError {std::move(assigned.error().message)});
        }
    }
    return {};
}

} // namespace

std::unique_ptr<DynamicSystemExecutor> make_script_system_executor(
    ScriptSystemCall call,
    bool checkpoint_safe_stateless
) {
    return std::make_unique<CallbackScriptSystemExecutor>(
        std::move(call),
        checkpoint_safe_stateless
    );
}

std::unique_ptr<DynamicConditionExecutor> make_script_condition_executor(
    ScriptConditionCall call,
    bool checkpoint_safe_stateless
) {
    return std::make_unique<CallbackScriptConditionExecutor>(
        std::move(call),
        checkpoint_safe_stateless
    );
}

Result<ScriptTypeBindings, ScriptError>
ensure_script_module_types(const ScriptModuleDecl& decl) {
    auto ordered = order_script_type_decls(decl);
    if (!ordered) {
        return failure(std::move(ordered.error()));
    }

    ScriptTypeBindings bindings;
    bindings.reserve(ordered->size());
    std::unordered_map<std::string, TypeId> script_types;
    script_types.reserve(ordered->size());
    auto& registry = Registry::instance();
    for (const auto* type_decl : *ordered) {
        std::vector<DynamicFieldDesc> fields;
        fields.reserve(type_decl->fields.size());
        for (const auto& field : type_decl->fields) {
            auto field_type = resolve_script_type_ref(field.type, script_types);
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

        auto registered = ensure_dynamic_struct(
            registry,
            DynamicStructDesc {
                .name = type_decl->qualified_name,
                .id = TypeId {type_decl->qualified_name},
                .fields = std::move(fields),
            }
        );
        if (!registered) {
            return failure(std::move(registered.error()));
        }

        script_types.emplace(type_decl->qualified_name, registered->id());
        bindings.push_back(
            ScriptTypeBinding {
                .local_name = type_decl->name,
                .qualified_name = type_decl->qualified_name,
                .type = &*registered,
            }
        );
    }
    return bindings;
}

Status<ScriptError>
install_script_module_resources(World& world, const ScriptModuleDecl& decl) {
    auto& registry = Registry::instance();
    for (const auto& resource : decl.resources) {
        auto type = registry.try_get_type(std::string_view {resource.type});
        if (!type) {
            return failure(ScriptError {std::move(type.error().message)});
        }
        if (resource.init_if_missing && world.has_resource(type->id())) {
            continue;
        }
        if (!type->default_constructible()) {
            return failure(
                ScriptError {
                    "Resource type '" + type->name() +
                    "' is not default constructible"
                }
            );
        }

        auto value = Val::default_construct(*type);
        auto initialized =
            apply_script_resource_initial_values(value, resource);
        if (!initialized) {
            return failure(std::move(initialized.error()));
        }
        world.add_resource(type->id(), std::move(value));
    }
    return {};
}

Result<SystemAccess, ScriptError>
script_system_access_for_decl(const DynamicSystemDecl& decl) {
    auto params = compile_dynamic_system_params(decl);
    if (!params) {
        return failure(ScriptError {std::move(params.error().message)});
    }
    return dynamic_system_access_for_params(*params);
}

SystemProfileInfo script_system_profile_for_decl(
    const ScriptModuleDecl& module_decl,
    const DynamicSystemDecl& system_decl
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

Result<std::vector<SystemHandle>, ScriptError> install_script_module_systems(
    World& world,
    const ScriptModuleDecl& decl,
    const ScriptSystemExecutorFactory& create_executor,
    ScriptSystemInstallOptions options
) {
    struct CompiledScriptSystem {
        const DynamicSystemDecl* decl {nullptr};
        DynamicSystemParams params;
        std::unique_ptr<DynamicSystemExecutor> executor;
        std::vector<std::unique_ptr<Condition>> conditions;
    };

    std::vector<CompiledScriptSystem> compiled_systems;
    compiled_systems.reserve(decl.systems.size());
    for (const auto& system : decl.systems) {
        if (system.name.empty()) {
            return failure(ScriptError {"Script system missing name"});
        }
        auto params = compile_dynamic_system_params(system);
        if (!params) {
            return failure(ScriptError {std::move(params.error().message)});
        }
        auto executor = create_executor(system);
        if (!executor) {
            return failure(std::move(executor.error()));
        }
        if (!*executor) {
            return failure(ScriptError {"Script system executor is null"});
        }
        std::vector<std::unique_ptr<Condition>> conditions;
        conditions.reserve(system.conditions.size());
        for (const auto& condition : system.conditions) {
            if (!options.create_condition_executor) {
                return failure(
                    ScriptError {
                        "Script condition executor factory is not configured"
                    }
                );
            }
            auto condition_params = compile_dynamic_condition_params(condition);
            if (!condition_params) {
                return failure(
                    ScriptError {std::move(condition_params.error().message)}
                );
            }
            auto condition_executor =
                options.create_condition_executor(condition);
            if (!condition_executor) {
                return failure(std::move(condition_executor.error()));
            }
            if (!*condition_executor) {
                return failure(
                    ScriptError {"Script condition executor is null"}
                );
            }
            conditions.push_back(
                std::make_unique<DynamicCondition>(
                    condition.name,
                    std::move(*condition_params),
                    std::move(*condition_executor)
                )
            );
        }
        compiled_systems.push_back(
            CompiledScriptSystem {
                .decl = &system,
                .params = std::move(*params),
                .executor = std::move(*executor),
                .conditions = std::move(conditions),
            }
        );
    }

    std::vector<SystemConfig> configs;
    configs.reserve(compiled_systems.size());
    for (auto& system : compiled_systems) {
        auto dynamic_system = std::make_unique<DynamicSystem>(
            system.decl->name,
            std::move(system.params),
            std::move(system.executor)
        );
        SystemConfig config(std::move(dynamic_system));
        config.conditions = std::move(system.conditions);
        config.profile = script_system_profile_for_decl(decl, *system.decl);
        config.main_thread_only = options.main_thread_only;
        configs.push_back(std::move(config));
    }

    auto dependency_index =
        [&](const DynamicSystemDecl& source,
            std::string_view target_name) -> Result<std::size_t, ScriptError> {
        Optional<std::size_t> result;
        for (std::size_t index = 0; index < decl.systems.size(); ++index) {
            const auto& target = decl.systems[index];
            if (target.schedule != source.schedule ||
                target.name != target_name) {
                continue;
            }
            if (result) {
                return failure(
                    ScriptError {
                        "Ambiguous script system dependency '" +
                        std::string(target_name) + "'"
                    }
                );
            }
            result = index;
        }
        if (!result) {
            return failure(
                ScriptError {
                    "Script system '" + source.name +
                    "' references missing dependency '" +
                    std::string(target_name) + "' in the same schedule"
                }
            );
        }
        return *result;
    };

    for (std::size_t index = 0; index < decl.systems.size(); ++index) {
        const auto& system = decl.systems[index];
        for (const auto& target_name : system.before) {
            auto target = dependency_index(system, target_name);
            if (!target) {
                return failure(std::move(target.error()));
            }
            configs[index].dependencies.before.insert(configs[*target].id);
        }
        for (const auto& target_name : system.after) {
            auto target = dependency_index(system, target_name);
            if (!target) {
                return failure(std::move(target.error()));
            }
            configs[index].dependencies.after.insert(configs[*target].id);
        }
    }

    std::vector<SystemHandle> handles;
    handles.reserve(compiled_systems.size());
    for (std::size_t index = 0; index < configs.size(); ++index) {
        handles.push_back(world.add_system(
            decl.systems[index].schedule,
            std::move(configs[index])
        ));
    }
    return handles;
}

Result<std::vector<SystemHandle>, ScriptError> install_script_module(
    World& world,
    const ScriptModuleDecl& decl,
    const ScriptTypeBinder& bind_type,
    const ScriptSystemExecutorFactory& create_executor,
    ScriptSystemInstallOptions options
) {
    auto bindings = ensure_script_module_types(decl);
    if (!bindings) {
        return failure(std::move(bindings.error()));
    }
    for (const auto& binding : *bindings) {
        auto bound = bind_type(binding);
        if (!bound) {
            return failure(std::move(bound.error()));
        }
    }

    auto states = install_script_module_states(world, decl);
    if (!states) {
        return failure(std::move(states.error()));
    }

    auto resources = install_script_module_resources(world, decl);
    if (!resources) {
        return failure(std::move(resources.error()));
    }
    return install_script_module_systems(world, decl, create_executor, options);
}

bool remove_script_module_systems(
    World& world,
    const std::vector<SystemHandle>& systems
) {
    bool removed_all_systems = true;
    for (auto handle : systems) {
        removed_all_systems =
            world.remove_system(handle) && removed_all_systems;
    }
    return removed_all_systems;
}

} // namespace fei
