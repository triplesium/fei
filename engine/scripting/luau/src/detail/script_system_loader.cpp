#include "scripting_luau/detail/script_system_loader.hpp"

#include "ecs/dynamic/system.hpp"
#include "refl/registry.hpp"
#include "scripting/module_install.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace fei::detail {
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
    return runtime.bind_module_type(module, type_ref.type_name, *type);
}

Status<ScriptError> bind_declared_types(
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
) {
    std::unordered_set<TypeId> bound;
    for (const auto& system : declaration.systems) {
        for (const auto& param : system.params) {
            if (param->decl_type_id() == type_id<DynamicWorldParamDecl>()) {
                continue;
            }
            if (param->decl_type_id() == type_id<DynamicResourceParamDecl>()) {
                const auto& resource =
                    static_cast<const DynamicResourceParamDecl&>(*param);
                auto status =
                    bind_type_ref(runtime, module, resource.type, bound);
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
                auto status = bind_type_ref(runtime, module, field.type, bound);
                if (!status) {
                    return status;
                }
            }
            for (const auto& filter : query.filters) {
                auto status =
                    bind_type_ref(runtime, module, filter.type, bound);
                if (!status) {
                    return status;
                }
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
        "Entity",      "Read",        "Write",        "With",
        "Without",     "module",      "system",       "MainSchedules",
        "First",       "PreStartUp",  "StartUp",      "PreUpdate",
        "Update",      "PostUpdate",  "Last",         "RenderPrepare",
        "RenderFirst", "RenderStart", "RenderUpdate", "RenderEnd",
        "RenderLast",
    };
    std::unordered_map<std::string, std::size_t> name_counts;
    for (const auto& [id, type] : Registry::instance().types()) {
        (void)id;
        ++name_counts[type.stripped_name()];
    }
    for (auto& [id, type] : Registry::instance().types()) {
        const auto& name = type.stripped_name();
        if (bound.contains(id) || name_counts[name] != 1 ||
            Registry::instance().enums().contains(id) ||
            reserved.contains(name) || !valid_identifier(name)) {
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
    return {};
}

} // namespace

Result<std::vector<SystemHandle>, ScriptError> install_luau_script_systems(
    World& world,
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const ScriptModuleDecl& declaration
) {
    auto declared_types = bind_declared_types(runtime, module, declaration);
    if (!declared_types) {
        return failure(std::move(declared_types.error()));
    }
    auto bind_type = [&](const ScriptTypeBinding& binding) {
        return runtime
            .bind_module_type(module, binding.local_name, *binding.type);
    };
    auto create_executor = [&](const DynamicSystemDecl& system)
        -> Result<std::unique_ptr<DynamicSystemExecutor>, ScriptError> {
        return make_script_system_executor(
            [&runtime,
             module,
             name = system.name](const std::vector<Ref>& args) {
                return runtime.call_module_function(module, name, args);
            }
        );
    };
    return install_script_module(
        world,
        declaration,
        bind_type,
        create_executor,
        ScriptSystemInstallOptions {.main_thread_only = true}
    );
}

} // namespace fei::detail
