#include "ecs/dynamic/system_decl.hpp"

#include "ecs/dynamic/commands.hpp"
#include "ecs/dynamic/query.hpp"
#include "ecs/dynamic/resource.hpp"
#include "ecs/fwd.hpp"
#include "refl/registry.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace fei {
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

    std::vector<DynamicQueryFilter> filters;
    filters.reserve(decl.filters.size());
    for (const auto& filter_decl : decl.filters) {
        if (filter_decl.type.type_name.empty() && !filter_decl.type.type_id) {
            return failure(
                DynamicSystemError {"Query dynamic system filter missing type"}
            );
        }

        auto type = resolve_dynamic_type_ref(filter_decl.type);
        if (!type) {
            return failure(std::move(type.error()));
        }
        filters.push_back(
            DynamicQueryFilter {
                .type = *type,
                .required = filter_decl.required,
            }
        );
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

void register_builtin_dynamic_system_param_compilers(
    DynamicSystemParamCompilerRegistry& registry
) {
    registry.add<DynamicResourceParamDecl>(&compile_dynamic_resource_param);
    registry.add<DynamicQueryParamDecl>(&compile_dynamic_query_param);
    registry.add<DynamicCommandsParamDecl>(&compile_dynamic_commands_param);
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
    return failure(DynamicSystemError {std::move(type.error().message)});
}

Result<DynamicSystemParamPtr, DynamicSystemError>
compile_dynamic_system_param(const DynamicSystemParamDecl& param) {
    return DynamicSystemParamCompilerRegistry::instance().compile(param);
}

Result<DynamicSystemParams, DynamicSystemError>
compile_dynamic_system_params(const DynamicSystemDecl& decl) {
    DynamicSystemParams params;
    params.reserve(decl.params.size());
    for (const auto& param : decl.params) {
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

} // namespace fei
