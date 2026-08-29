#include "scripting/detail/plugin_install.hpp"

#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/system_decl.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/dynamic_type.hpp"
#include "refl/registry.hpp"
#include "scripting/detail/state.hpp"

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace ets {
namespace {

class CallbackLuauSystemExecutor final : public DynamicSystemExecutor {
  private:
    LuauSystemCall m_call;
    bool m_checkpoint_safe_stateless {false};

  public:
    CallbackLuauSystemExecutor(
        LuauSystemCall call,
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

class CallbackLuauConditionExecutor final : public DynamicConditionExecutor {
  private:
    LuauConditionCall m_call;
    bool m_checkpoint_safe_stateless {false};

  public:
    CallbackLuauConditionExecutor(
        LuauConditionCall call,
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

Result<TypeId, LuauScriptError> resolve_luau_type_ref(
    const LuauTypeRef& type_ref,
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
            return failure(LuauScriptError {std::move(type.error().message)});
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
        LuauScriptError {
            "Optional script fields currently support only Entity values",
        }
    );
}

Status<LuauScriptError> append_luau_type_decl(
    const LuauTypeDecl& type_decl,
    const std::unordered_map<std::string, const LuauTypeDecl*>& type_decls,
    std::unordered_map<std::string, int>& visit_state,
    std::vector<const LuauTypeDecl*>& ordered
) {
    auto& state = visit_state[type_decl.qualified_name];
    if (state == 2) {
        return {};
    }
    if (state == 1) {
        return failure(
            LuauScriptError {
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
        auto status = append_luau_type_decl(
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

Result<std::vector<const LuauTypeDecl*>, LuauScriptError>
order_luau_type_decls(const LuauModuleSchema& decl) {
    std::unordered_map<std::string, const LuauTypeDecl*> type_decls;
    type_decls.reserve(decl.types.size());
    for (const auto& type_decl : decl.types) {
        if (!type_decls.emplace(type_decl.qualified_name, &type_decl).second) {
            return failure(
                LuauScriptError {
                    "Duplicate script-defined type '" +
                    type_decl.qualified_name + "'"
                }
            );
        }
    }

    std::unordered_map<std::string, int> visit_state;
    std::vector<const LuauTypeDecl*> ordered;
    ordered.reserve(decl.types.size());
    for (const auto& type_decl : decl.types) {
        auto status =
            append_luau_type_decl(type_decl, type_decls, visit_state, ordered);
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

Result<Type&, LuauScriptError>
ensure_dynamic_struct(Registry& registry, DynamicStructDesc desc) {
    if (auto existing = registry.try_get_type(desc.id)) {
        const auto* layout = registry.try_get_dynamic_struct_layout(desc.id);
        if (layout && layouts_match(*layout, desc)) {
            return *existing;
        }
        return failure(
            LuauScriptError {
                "Script-defined type schema conflicts with existing type '" +
                existing->name() + "'"
            }
        );
    }

    auto registered = registry.register_dynamic_struct(std::move(desc));
    if (!registered) {
        return failure(LuauScriptError {std::move(registered.error().message)});
    }
    return *registered;
}

} // namespace

std::unique_ptr<DynamicSystemExecutor>
make_luau_system_executor(LuauSystemCall call, bool checkpoint_safe_stateless) {
    return std::make_unique<CallbackLuauSystemExecutor>(
        std::move(call),
        checkpoint_safe_stateless
    );
}

std::unique_ptr<DynamicConditionExecutor> make_luau_condition_executor(
    LuauConditionCall call,
    bool checkpoint_safe_stateless
) {
    return std::make_unique<CallbackLuauConditionExecutor>(
        std::move(call),
        checkpoint_safe_stateless
    );
}

Result<LuauTypeBindings, LuauScriptError>
ensure_luau_types(const LuauModuleSchema& decl) {
    auto ordered = order_luau_type_decls(decl);
    if (!ordered) {
        return failure(std::move(ordered.error()));
    }

    LuauTypeBindings bindings;
    bindings.reserve(decl.enums.size() + ordered->size());
    std::unordered_map<std::string, TypeId> script_types;
    script_types.reserve(decl.enums.size() + ordered->size());
    auto& registry = Registry::instance();
    for (const auto& enumeration : decl.enums) {
        auto ensured = ensure_luau_enum_type(enumeration);
        if (!ensured) {
            return failure(std::move(ensured.error()));
        }
        auto type = registry.try_get_type(enumeration.type_id);
        if (!type) {
            return failure(LuauScriptError {std::move(type.error().message)});
        }
        script_types.emplace(enumeration.qualified_name, enumeration.type_id);
        bindings.push_back(
            LuauTypeBinding {
                .local_name = enumeration.name,
                .qualified_name = enumeration.qualified_name,
                .type = &*type,
            }
        );
    }
    for (const auto* type_decl : *ordered) {
        std::vector<DynamicFieldDesc> fields;
        fields.reserve(type_decl->fields.size());
        for (const auto& field : type_decl->fields) {
            auto field_type = resolve_luau_type_ref(field.type, script_types);
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
            LuauTypeBinding {
                .local_name = type_decl->name,
                .qualified_name = type_decl->qualified_name,
                .type = &*registered,
            }
        );
    }
    return bindings;
}

Result<SystemAccess, LuauScriptError>
luau_system_access_for_decl(const DynamicSystemDecl& decl) {
    auto params = compile_dynamic_system_params(decl);
    if (!params) {
        return failure(LuauScriptError {std::move(params.error().message)});
    }
    return dynamic_system_access_for_params(*params);
}

SystemProfileInfo luau_system_profile_for_decl(
    std::string_view source_name,
    const DynamicSystemDecl& system_decl
) {
    const auto file = source_name.empty() ? std::string {"<script>"} :
                                            std::string {source_name};
    return SystemProfileInfo {
        .name = file + "::" + system_decl.name,
        .file = file,
        .function = system_decl.name,
        .line = 0,
    };
}

Result<std::vector<SystemHandle>, LuauScriptError> install_luau_systems(
    World& world,
    std::string_view source_name,
    const std::vector<DynamicSystemDecl>& systems,
    const LuauSystemExecutorFactory& create_executor,
    LuauSystemInstallOptions options
) {
    struct CompiledScriptSystem {
        const DynamicSystemDecl* decl {nullptr};
        DynamicSystemParams params;
        std::unique_ptr<DynamicSystemExecutor> executor;
        std::vector<std::unique_ptr<Condition>> conditions;
    };

    std::vector<CompiledScriptSystem> compiled_systems;
    compiled_systems.reserve(systems.size());
    for (const auto& system : systems) {
        if (system.name.empty()) {
            return failure(LuauScriptError {"Script system missing name"});
        }
        auto params = compile_dynamic_system_params(system);
        if (!params) {
            return failure(LuauScriptError {std::move(params.error().message)});
        }
        auto executor = create_executor(system);
        if (!executor) {
            return failure(std::move(executor.error()));
        }
        if (!*executor) {
            return failure(LuauScriptError {"Script system executor is null"});
        }
        std::vector<std::unique_ptr<Condition>> conditions;
        conditions.reserve(system.conditions.size());
        for (const auto& condition : system.conditions) {
            if (!options.create_condition_executor) {
                return failure(
                    LuauScriptError {
                        "Script condition executor factory is not configured"
                    }
                );
            }
            auto condition_params = compile_dynamic_condition_params(condition);
            if (!condition_params) {
                return failure(
                    LuauScriptError {
                        std::move(condition_params.error().message)
                    }
                );
            }
            auto condition_access =
                dynamic_system_access_for_params(*condition_params);
            if (!condition_access.write_resources.empty() ||
                !condition_access.write_components.empty() ||
                condition_access.world_exclusive || condition_access.commands ||
                condition_access.deferred_commands) {
                return failure(
                    LuauScriptError {
                        "Script condition '" + condition.name +
                            "' must use only read-only parameters",
                    }
                );
            }
            auto condition_executor =
                options.create_condition_executor(condition);
            if (!condition_executor) {
                return failure(std::move(condition_executor.error()));
            }
            if (!*condition_executor) {
                return failure(
                    LuauScriptError {"Script condition executor is null"}
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
        config.profile =
            luau_system_profile_for_decl(source_name, *system.decl);
        config.main_thread_only = options.main_thread_only;
        configs.push_back(std::move(config));
    }

    auto dependency_index = [&](
                                const DynamicSystemDecl& source,
                                std::string_view target_name
                            ) -> Result<std::size_t, LuauScriptError> {
        Optional<std::size_t> result;
        for (std::size_t index = 0; index < systems.size(); ++index) {
            const auto& target = systems[index];
            if (target.schedule != source.schedule ||
                target.name != target_name) {
                continue;
            }
            if (result) {
                return failure(
                    LuauScriptError {
                        "Ambiguous script system dependency '" +
                        std::string(target_name) + "'"
                    }
                );
            }
            result = index;
        }
        if (!result) {
            return failure(
                LuauScriptError {
                    "Script system '" + source.name +
                    "' references missing dependency '" +
                    std::string(target_name) + "' in the same schedule"
                }
            );
        }
        return *result;
    };

    std::vector<std::vector<std::size_t>> dependency_edges(systems.size());
    for (std::size_t index = 0; index < systems.size(); ++index) {
        const auto& system = systems[index];
        for (const auto& target_name : system.before) {
            auto target = dependency_index(system, target_name);
            if (!target) {
                return failure(std::move(target.error()));
            }
            configs[index].dependencies.before.insert(configs[*target].id);
            dependency_edges[index].push_back(*target);
        }
        for (const auto& target_name : system.after) {
            auto target = dependency_index(system, target_name);
            if (!target) {
                return failure(std::move(target.error()));
            }
            configs[index].dependencies.after.insert(configs[*target].id);
            dependency_edges[*target].push_back(index);
        }
    }

    std::vector<int> dependency_states(systems.size());
    const auto visit_dependency = [&](const auto& self, std::size_t system) {
        if (dependency_states[system] == 1) {
            return false;
        }
        if (dependency_states[system] == 2) {
            return true;
        }
        dependency_states[system] = 1;
        for (const auto target : dependency_edges[system]) {
            if (!self(self, target)) {
                return false;
            }
        }
        dependency_states[system] = 2;
        return true;
    };
    for (std::size_t index = 0; index < systems.size(); ++index) {
        if (!visit_dependency(visit_dependency, index)) {
            return failure(
                LuauScriptError {"Cycle detected in script system dependencies"}
            );
        }
    }

    std::vector<SystemHandle> handles;
    handles.reserve(compiled_systems.size());
    for (std::size_t index = 0; index < configs.size(); ++index) {
        handles.push_back(
            world.add_system(systems[index].schedule, std::move(configs[index]))
        );
    }
    return handles;
}

bool remove_luau_plugin_systems(
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

} // namespace ets
