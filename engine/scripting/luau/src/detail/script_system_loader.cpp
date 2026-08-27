#include "scripting_luau/detail/script_system_loader.hpp"

#include "ecs/dynamic/state.hpp"
#include "ecs/dynamic/system.hpp"
#include "refl/registry.hpp"
#include "scripting/annotations.hpp"
#include "scripting/module_install.hpp"
#include "scripting/reflection_bridge.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ets::detail {
namespace {

Status<ScriptError> bind_type_ref(
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const DynamicTypeRef& type_ref,
    std::unordered_set<TypeId>& bound
) {
    auto resolved = resolve_dynamic_type_ref(type_ref);
    if (!resolved) {
        return failure(ScriptError {std::move(resolved.error().message)});
    }
    if (!bound.insert(*resolved).second) {
        return {};
    }
    auto type = Registry::instance().try_get_type(*resolved);
    if (!type) {
        return failure(ScriptError {std::move(type.error().message)});
    }
    if (type->has_structured_name()) {
        if (!is_script_visible(*type)) {
            return failure(
                ScriptError {
                    "Type '" + type->name() + "' is not visible to scripts",
                }
            );
        }
        if (type_ref.type_name == type->name()) {
            return {};
        }
        if (!is_script_prelude(*type)) {
            const auto annotation =
                type->annotation<annotations::ScriptModule>();
            std::string message =
                "Type '" + type->name() + "' is not in ScriptPrelude";
            if (annotation) {
                message += "; require(\"@entisium/";
                message += annotation->name;
                message += "\") and qualify the type through that local";
            }
            return failure(ScriptError {std::move(message)});
        }
        return runtime.bind_module_script_type(module, *type);
    }
    return runtime.bind_module_type(module, type_ref.type_name, *type);
}

Status<ScriptError> bind_declared_types(
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
) {
    std::unordered_set<TypeId> bound;
    std::unordered_set<std::string_view> script_types;
    script_types.reserve(declaration.types.size());
    for (const auto& type : declaration.types) {
        script_types.insert(type.qualified_name);
    }
    for (const auto& state : declaration.states) {
        script_types.insert(state.qualified_name);
        bound.insert(state.type_id);
    }
    auto bind_params = [&](const auto& params) -> Status<ScriptError> {
        for (const auto& param : params) {
            if (param->decl_type_id() == type_id<DynamicWorldParamDecl>()) {
                continue;
            }
            if (param->decl_type_id() == type_id<DynamicResourceParamDecl>()) {
                const auto& resource =
                    static_cast<const DynamicResourceParamDecl&>(*param);
                if (script_types.contains(resource.type.type_name)) {
                    continue;
                }
                auto status =
                    bind_type_ref(runtime, module, resource.type, bound);
                if (!status) {
                    return status;
                }
                continue;
            }
            if (param->decl_type_id() == type_id<DynamicStateParamDecl>() ||
                param->decl_type_id() == type_id<DynamicNextStateParamDecl>()) {
                const auto& type =
                    param->decl_type_id() == type_id<DynamicStateParamDecl>() ?
                        static_cast<const DynamicStateParamDecl&>(*param).type :
                        static_cast<const DynamicNextStateParamDecl&>(*param)
                            .type;
                if (script_types.contains(type.type_name)) {
                    continue;
                }
                auto status = bind_type_ref(runtime, module, type, bound);
                if (!status) {
                    return status;
                }
                continue;
            }
            if (param->decl_type_id() != type_id<DynamicQueryParamDecl>()) {
                continue;
            }
            const auto& query =
                static_cast<const DynamicQueryParamDecl&>(*param);
            for (const auto& field : query.fields) {
                if (field.kind == DynamicQueryFieldDeclKind::Entity) {
                    continue;
                }
                if (script_types.contains(field.type.type_name)) {
                    continue;
                }
                auto status = bind_type_ref(runtime, module, field.type, bound);
                if (!status) {
                    return status;
                }
            }
            for (const auto& filter : query.filters) {
                if (script_types.contains(filter.type.type_name)) {
                    continue;
                }
                auto status =
                    bind_type_ref(runtime, module, filter.type, bound);
                if (!status) {
                    return status;
                }
            }
        }
        return {};
    };
    for (const auto& system : declaration.systems) {
        auto status = bind_params(system.params);
        if (!status) {
            return status;
        }
        for (const auto& condition : system.conditions) {
            status = bind_params(condition.params);
            if (!status) {
                return status;
            }
        }
    }

    auto valid_identifier = [](std::string_view name) {
        if (name.empty() ||
            (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
             name.front() != '_')) {
            return false;
        }
        return std::ranges::all_of(name.substr(1), [](char character) {
            return std::isalnum(static_cast<unsigned char>(character)) ||
                   character == '_';
        });
    };
    static const std::unordered_set<std::string_view> reserved {
        "Entity",
        "Read",
        "Write",
        "With",
        "Without",
        "module",
        "system",
        "chain",
        "in_state",
        "OnEnter",
        "OnExit",
        "OnTransition",
        "State",
        "NextState",
        "MainSchedules",
        "First",
        "PreStartUp",
        "StartUp",
        "PreUpdate",
        "Update",
        "PostUpdate",
        "Last",
        "RenderPrepare",
        "RenderFirst",
        "RenderStart",
        "RenderUpdate",
        "RenderEnd",
        "RenderLast",
        "RunFixedMainLoop",
        "FixedFirst",
        "FixedPreUpdate",
        "FixedUpdate",
        "FixedPostUpdate",
        "FixedLast",
    };
    std::unordered_map<std::string, std::size_t> name_counts;
    for (const auto& [id, type] : Registry::instance().types()) {
        (void)id;
        ++name_counts[type.stripped_name()];
    }
    for (auto& [id, type] : Registry::instance().types()) {
        if (bound.contains(id) || Registry::instance().enums().contains(id)) {
            continue;
        }
        if (type.has_structured_name()) {
            if (!is_script_prelude(type)) {
                continue;
            }
            auto status = runtime.bind_module_script_type(module, type);
            if (!status) {
                return status;
            }
            bound.insert(id);
            continue;
        }
        const auto& name = type.stripped_name();
        if (name_counts[name] != 1 || reserved.contains(name) ||
            !valid_identifier(name)) {
            continue;
        }
        auto status = runtime.bind_module_type(module, name, type);
        if (!status) {
            return status;
        }
        bound.insert(id);
    }
    for (const auto& [id, enm] : Registry::instance().enums()) {
        auto type = Registry::instance().try_get_type(id);
        if (!type) {
            return failure(ScriptError {std::move(type.error().message)});
        }
        if (type->has_structured_name()) {
            if (!is_script_prelude(*type)) {
                continue;
            }
            auto status = runtime.bind_module_script_enum(module, enm);
            if (!status) {
                return status;
            }
            continue;
        }
        const auto& name = type->stripped_name();
        if (name_counts[name] != 1 || reserved.contains(name) ||
            !valid_identifier(name)) {
            continue;
        }
        auto status = runtime.bind_module_enum(module, name, enm);
        if (!status) {
            return status;
        }
    }
    return runtime.seal_module_script_namespaces(module);
}

} // namespace

Status<ScriptError> prepare_luau_script_system_module(
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
) {
    auto declared_types = bind_declared_types(runtime, module, declaration);
    if (!declared_types) {
        return failure(std::move(declared_types.error()));
    }
    auto bindings = ensure_script_module_types(declaration);
    if (!bindings) {
        return failure(std::move(bindings.error()));
    }
    for (const auto& binding : *bindings) {
        auto bound = runtime.bind_module_exported_type(
            module,
            binding.local_name,
            *binding.type
        );
        if (!bound) {
            return failure(std::move(bound.error()));
        }
    }
    return {};
}

Result<std::vector<SystemHandle>, ScriptError> install_luau_script_systems(
    World& world,
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
) {
    auto prepared =
        prepare_luau_script_system_module(runtime, module, declaration);
    if (!prepared) {
        return failure(std::move(prepared.error()));
    }
    auto create_executor = [&](const DynamicSystemDecl& system)
        -> Result<std::unique_ptr<DynamicSystemExecutor>, ScriptError> {
        return make_script_system_executor(
            [&runtime,
             module,
             name = system.name](const std::vector<Ref>& args) {
                return runtime.call_module_function(module, name, args);
            },
            true
        );
    };
    auto create_condition_executor = [&](const DynamicConditionDecl& condition)
        -> Result<std::unique_ptr<DynamicConditionExecutor>, ScriptError> {
        if (condition.kind == DynamicConditionDeclKind::InState) {
            if (!condition.state_value) {
                return failure(
                    ScriptError {"in_state condition is missing its value"}
                );
            }
            Val expected = *condition.state_value;
            return make_script_condition_executor(
                [expected = std::move(expected)](const std::vector<Ref>& args)
                    -> Result<bool, ScriptError> {
                    if (args.size() != 1) {
                        return failure(
                            ScriptError {"in_state condition expected one "
                                         "State parameter"}
                        );
                    }
                    const auto* state =
                        args[0].try_get_const<DynamicStateRef>();
                    if (state == nullptr) {
                        return failure(
                            ScriptError {
                                "in_state condition received an invalid State "
                                "parameter"
                            }
                        );
                    }
                    Ref current = state->get();
                    if (!current || current.type_id() != expected.type_id()) {
                        return false;
                    }
                    auto equal = expected.type()->equals(
                        current.const_ptr(),
                        expected.ref().const_ptr()
                    );
                    if (!equal) {
                        return failure(
                            ScriptError {
                                "State type is not equality comparable"
                            }
                        );
                    }
                    return *equal;
                },
                true
            );
        }
        return make_script_condition_executor(
            [&runtime,
             module,
             name = condition.name](const std::vector<Ref>& args) {
                return runtime.call_module_condition(module, name, args);
            },
            true
        );
    };
    return install_script_module(
        world,
        declaration,
        [](const ScriptTypeBinding&) -> Status<ScriptError> {
            return {};
        },
        create_executor,
        ScriptSystemInstallOptions {
            .main_thread_only = true,
            .create_condition_executor = create_condition_executor,
        }
    );
}

Result<std::vector<SystemHandle>, ScriptError> install_luau_script_systems(
    World& world,
    LuauExecutionPool& execution_pool,
    std::shared_ptr<const LuauExecutionModule> module,
    const ScriptModuleDecl& declaration
) {
    if (!module) {
        return failure(ScriptError {"Luau execution module is null"});
    }
    auto create_executor = [&](const DynamicSystemDecl& system)
        -> Result<std::unique_ptr<DynamicSystemExecutor>, ScriptError> {
        return make_script_system_executor(
            [&execution_pool,
             module,
             name = system.name](const std::vector<Ref>& args) {
                return execution_pool.call_module_function(*module, name, args);
            },
            true
        );
    };
    auto create_condition_executor = [&](const DynamicConditionDecl& condition)
        -> Result<std::unique_ptr<DynamicConditionExecutor>, ScriptError> {
        if (condition.kind == DynamicConditionDeclKind::InState) {
            if (!condition.state_value) {
                return failure(
                    ScriptError {"in_state condition is missing its value"}
                );
            }
            Val expected = *condition.state_value;
            return make_script_condition_executor(
                [expected = std::move(expected)](const std::vector<Ref>& args)
                    -> Result<bool, ScriptError> {
                    if (args.size() != 1) {
                        return failure(
                            ScriptError {"in_state condition expected one "
                                         "State parameter"}
                        );
                    }
                    const auto* state =
                        args[0].try_get_const<DynamicStateRef>();
                    if (state == nullptr) {
                        return failure(
                            ScriptError {
                                "in_state condition received an invalid State "
                                "parameter"
                            }
                        );
                    }
                    Ref current = state->get();
                    if (!current || current.type_id() != expected.type_id()) {
                        return false;
                    }
                    auto equal = expected.type()->equals(
                        current.const_ptr(),
                        expected.ref().const_ptr()
                    );
                    if (!equal) {
                        return failure(
                            ScriptError {
                                "State type is not equality comparable"
                            }
                        );
                    }
                    return *equal;
                },
                true
            );
        }
        return make_script_condition_executor(
            [&execution_pool,
             module,
             name = condition.name](const std::vector<Ref>& args) {
                return execution_pool
                    .call_module_condition(*module, name, args);
            },
            true
        );
    };
    return install_script_module(
        world,
        declaration,
        [](const ScriptTypeBinding&) -> Status<ScriptError> {
            return {};
        },
        create_executor,
        ScriptSystemInstallOptions {
            .main_thread_only = false,
            .create_condition_executor = create_condition_executor,
        }
    );
}

} // namespace ets::detail
