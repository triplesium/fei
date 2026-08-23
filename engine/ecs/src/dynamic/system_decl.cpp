#include "ecs/dynamic/system_decl.hpp"

#include "ecs/dynamic/commands.hpp"
#include "ecs/dynamic/events.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/removed_components.hpp"
#include "ecs/dynamic/resource.hpp"
#include "ecs/dynamic/state.hpp"
#include "ecs/dynamic/world.hpp"
#include "ecs/fwd.hpp"
#include "refl/registry.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ets {
namespace {

Optional<TypeId> primitive_dynamic_type_id(std::string_view name) {
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

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_resource_param(const DynamicResourceParamDecl& decl) {
    if (decl.type.type_name.empty() && !decl.type.type_id) {
        return failure(
            DynamicSystemError {"Resource dynamic system param missing type"}
        );
    }

    auto type = resolve_dynamic_type_ref(decl.type);
    if (!type) {
        return failure(std::move(type.error()));
    }

    DynamicSystemParamPtr result = std::make_unique<DynamicResourceParam>(
        decl.name,
        *type,
        decl.access,
        decl.optional
    );
    return std::move(result);
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_query_param(const DynamicQueryParamDecl& decl) {
    if (decl.name.empty()) {
        return failure(
            DynamicSystemError {"Query dynamic system param missing name"}
        );
    }

    std::vector<DynamicQueryField> fields;
    fields.reserve(decl.fields.size());
    for (const auto& field_decl : decl.fields) {
        if (field_decl.kind == DynamicQueryFieldDeclKind::Entity) {
            if (field_decl.name.empty()) {
                return failure(
                    DynamicSystemError {
                        "Query entity dynamic system param missing name"
                    }
                );
            }
            fields.push_back(
                DynamicQueryField {
                    .name = field_decl.name,
                    .type = type_id<Entity>(),
                    .access = DynamicParamAccess::Read,
                    .kind = DynamicQueryFieldKind::Entity,
                }
            );
            continue;
        }

        if (field_decl.type.type_name.empty() && !field_decl.type.type_id) {
            return failure(
                DynamicSystemError {"Query dynamic system field missing type"}
            );
        }
        if (field_decl.name.empty()) {
            return failure(
                DynamicSystemError {
                    "Query component dynamic system param missing name"
                }
            );
        }

        auto type = resolve_dynamic_type_ref(field_decl.type);
        if (!type) {
            return failure(std::move(type.error()));
        }
        fields.push_back(
            DynamicQueryField {
                .name = field_decl.name,
                .type = *type,
                .access = field_decl.access,
            }
        );
    }

    const auto compile_filter = [&](const auto& self,
                                    const DynamicQueryFilterDecl& filter_decl)
        -> Result<DynamicQueryFilter, DynamicSystemError> {
        using DeclKind = DynamicQueryFilterDecl::Kind;
        using Kind = DynamicQueryFilter::Kind;
        if (filter_decl.kind == DeclKind::Or) {
            if (filter_decl.filters.empty()) {
                return failure(
                    DynamicSystemError {
                        "Or query filter must contain at least one filter",
                    }
                );
            }
            DynamicQueryFilter result {.kind = Kind::Or};
            result.filters.reserve(filter_decl.filters.size());
            for (const auto& child : filter_decl.filters) {
                auto compiled = self(self, child);
                if (!compiled) {
                    return failure(std::move(compiled.error()));
                }
                result.filters.push_back(std::move(*compiled));
            }
            return result;
        }
        if (filter_decl.type.type_name.empty() && !filter_decl.type.type_id) {
            return failure(
                DynamicSystemError {"Query dynamic system filter missing type"}
            );
        }

        auto type = resolve_dynamic_type_ref(filter_decl.type);
        if (!type) {
            return failure(std::move(type.error()));
        }
        Kind kind = Kind::With;
        switch (filter_decl.kind) {
            case DeclKind::With:
                kind = Kind::With;
                break;
            case DeclKind::Without:
                kind = Kind::Without;
                break;
            case DeclKind::Added:
                kind = Kind::Added;
                break;
            case DeclKind::Changed:
                kind = Kind::Changed;
                break;
            case DeclKind::Or:
                break;
        }
        return DynamicQueryFilter {
            .kind = kind,
            .type = *type,
            .required = filter_decl.required,
        };
    };

    std::vector<DynamicQueryFilter> filters;
    filters.reserve(decl.filters.size());
    for (const auto& filter_decl : decl.filters) {
        auto filter = compile_filter(compile_filter, filter_decl);
        if (!filter) {
            return failure(std::move(filter.error()));
        }
        filters.push_back(std::move(*filter));
    }

    if (fields.empty()) {
        return failure(
            DynamicSystemError {
                "Query dynamic system param must declare fields"
            }
        );
    }

    DynamicSystemParamPtr result = std::make_unique<DynamicQuery>(
        decl.name,
        std::move(fields),
        std::move(filters)
    );
    return std::move(result);
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_commands_param(const DynamicCommandsParamDecl& decl) {
    if (decl.name.empty()) {
        return failure(
            DynamicSystemError {"Commands dynamic system param missing name"}
        );
    }

    DynamicSystemParamPtr result =
        std::make_unique<DynamicCommandsParam>(decl.name);
    return std::move(result);
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_world_param(const DynamicWorldParamDecl& decl) {
    if (decl.name.empty()) {
        return failure(
            DynamicSystemError {"World dynamic system param missing name"}
        );
    }

    DynamicSystemParamPtr result = std::make_unique<DynamicWorld>(decl.name);
    return std::move(result);
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_removed_components_param(
    const DynamicRemovedComponentsParamDecl& decl
) {
    auto type = resolve_dynamic_type_ref(decl.type);
    if (!type) {
        return failure(std::move(type.error()));
    }
    DynamicSystemParamPtr result =
        std::make_unique<DynamicRemovedComponents>(decl.name, *type);
    return result;
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_event_param(const DynamicEventParamDecl& decl) {
    auto type = resolve_dynamic_type_ref(decl.type);
    if (!type) {
        return failure(std::move(type.error()));
    }
    auto kind = DynamicEventParamKind::ReaderRO;
    switch (decl.kind) {
        case DynamicEventParamDeclKind::Writer:
            kind = DynamicEventParamKind::Writer;
            break;
        case DynamicEventParamDeclKind::Reader:
            kind = DynamicEventParamKind::Reader;
            break;
        case DynamicEventParamDeclKind::ReaderRO:
            kind = DynamicEventParamKind::ReaderRO;
            break;
    }
    DynamicSystemParamPtr result = std::make_unique<DynamicEventParam>(
        decl.name,
        *type,
        kind,
        decl.optional
    );
    return result;
}

template<typename Param, typename Decl>
Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_state_param(const Decl& decl) {
    auto type = resolve_dynamic_type_ref(decl.type);
    if (!type) {
        return failure(std::move(type.error()));
    }
    auto ops = resolve_dynamic_state(*type);
    if (!ops) {
        return failure(std::move(ops.error()));
    }
    DynamicSystemParamPtr result = std::make_unique<Param>(*ops);
    return std::move(result);
}

void register_builtin_dynamic_system_param_compilers(
    DynamicSystemParamCompilerRegistry& registry
) {
    registry.add<DynamicResourceParamDecl>(&compile_dynamic_resource_param);
    registry.add<DynamicQueryParamDecl>(&compile_dynamic_query_param);
    registry.add<DynamicCommandsParamDecl>(&compile_dynamic_commands_param);
    registry.add<DynamicWorldParamDecl>(&compile_dynamic_world_param);
    registry.add<DynamicStateParamDecl>(
        &compile_dynamic_state_param<DynamicStateParam, DynamicStateParamDecl>
    );
    registry.add<DynamicNextStateParamDecl>(&compile_dynamic_state_param<
                                            DynamicNextStateParam,
                                            DynamicNextStateParamDecl>);
    registry.add<DynamicRemovedComponentsParamDecl>(
        &compile_dynamic_removed_components_param
    );
    registry.add<DynamicEventParamDecl>(&compile_dynamic_event_param);
}

} // namespace

DynamicSystemParamCompilerRegistry&
DynamicSystemParamCompilerRegistry::instance() {
    static DynamicSystemParamCompilerRegistry registry = [] {
        DynamicSystemParamCompilerRegistry result;
        register_builtin_dynamic_system_param_compilers(result);
        return result;
    }();
    return registry;
}

Result<DynamicSystemParamPtr, DynamicSystemError>
DynamicSystemParamCompilerRegistry::compile(
    const DynamicSystemParamDecl& decl
) const {
    auto it = m_compilers.find(decl.decl_type_id());
    if (it == m_compilers.end()) {
        return failure(
            DynamicSystemError {
                "Dynamic system param compiler not registered for '" +
                std::string(decl.decl_type_name()) + "'"
            }
        );
    }
    return it->second(decl);
}

Result<TypeId, DynamicSystemError>
resolve_dynamic_type_ref(const DynamicTypeRef& type_ref) {
    if (type_ref.type_id) {
        return *type_ref.type_id;
    }
    if (auto primitive = primitive_dynamic_type_id(type_ref.type_name)) {
        return *primitive;
    }

    auto type = Registry::instance().try_get_type(
        std::string_view {type_ref.type_name}
    );
    if (type) {
        return type->id();
    }
    if (type_ref.type_name.contains("::") &&
        !type_ref.type_name.starts_with("ets::")) {
        auto rooted = Registry::instance().try_get_type_exact(
            "ets::" + type_ref.type_name
        );
        if (rooted) {
            return rooted->id();
        }
    }
    return failure(DynamicSystemError {std::move(type.error().message)});
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_system_param(const DynamicSystemParamDecl& param) {
    return DynamicSystemParamCompilerRegistry::instance().compile(param);
}

static Result<DynamicSystemParams, DynamicSystemError> compile_dynamic_params(
    const std::vector<DynamicSystemParamDeclPtr>& declarations
) {
    DynamicSystemParams params;
    params.reserve(declarations.size());
    for (const auto& param : declarations) {
        if (!param) {
            return failure(
                DynamicSystemError {"Dynamic system param decl is null"}
            );
        }
        auto compiled = compile_dynamic_system_param(*param);
        if (!compiled) {
            return failure(std::move(compiled.error()));
        }
        params.push_back(std::move(*compiled));
    }
    return std::move(params);
}

Result<DynamicSystemParams, DynamicSystemError>
compile_dynamic_system_params(const DynamicSystemDecl& decl) {
    return compile_dynamic_params(decl.params);
}

Result<DynamicSystemParams, DynamicSystemError>
compile_dynamic_condition_params(const DynamicConditionDecl& decl) {
    return compile_dynamic_params(decl.params);
}

} // namespace ets
