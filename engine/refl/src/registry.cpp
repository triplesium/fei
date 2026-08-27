#include "refl/registry.hpp"

#include "base/log.hpp"
#include "container_method.hpp"
#include "dynamic_container_adapter.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/type.hpp"

namespace ets {

Registry* Registry::s_instance = nullptr;
Registry& Registry::instance() {
    if (!s_instance) {
        s_instance = new Registry();
        register_dynamic_container_adapters(*s_instance);
    }
    return *s_instance;
}

Type& Registry::register_type(
    TypeId id,
    const std::string& name,
    std::size_t size,
    std::size_t align,
    TypeOps ops
) {
    auto name_it = m_type_ids_by_name.find(name);
    if (name_it != m_type_ids_by_name.end() && name_it->second != id) {
        fatal(
            "Type name collision for '{}': id {} conflicts with id {}",
            name,
            id.id(),
            name_it->second.id()
        );
    }

    auto it = m_types.find(id);
    if (it != m_types.end()) {
        if (it->second.name() != name) {
            fatal(
                "TypeId collision for id {}: '{}' conflicts with '{}'",
                id.id(),
                name,
                it->second.name()
            );
        }
        m_type_ids_by_name.emplace(name, id);
        return it->second;
    }
    Type type(name, id, size, align, ops);
    m_types.emplace(id, type);
    m_type_ids_by_name.emplace(name, id);
    return m_types.at(id);
}

Type& Registry::set_structured_type_name(
    TypeId id,
    std::initializer_list<std::string_view> namespace_path,
    std::string_view local_name
) {
    auto& registered = get_type(id);
    if (local_name.empty()) {
        fatal("Type '{}' has an empty local name", registered.name());
    }

    std::vector<std::string> path;
    path.reserve(namespace_path.size());
    for (const auto component : namespace_path) {
        if (component.empty() ||
            component.find("::") != std::string_view::npos) {
            fatal(
                "Type '{}' has invalid namespace component '{}'",
                registered.name(),
                component
            );
        }
        path.emplace_back(component);
    }

    if (registered.m_has_structured_name) {
        if (registered.m_namespace_path != path ||
            registered.m_local_name != local_name) {
            fatal(
                "Type '{}' has conflicting structured names",
                registered.name()
            );
        }
        return registered;
    }

    registered.m_namespace_path = std::move(path);
    registered.m_local_name = local_name;
    registered.m_has_structured_name = true;
    return registered;
}

Type& Registry::get_type(TypeId id) {
    auto result = try_get_type(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Optional<std::string> Registry::registered_type_name(TypeId id) const {
    auto it = m_types.find(id);
    if (it == m_types.end()) {
        return nullopt;
    }
    return it->second.name();
}

Result<Type&, RegistryError> Registry::try_get_type(TypeId id) {
    auto it = m_types.find(id);
    if (it == m_types.end()) {
        return failure(RegistryError::type_not_found(id));
    }
    return it->second;
}

Result<Type&, RegistryError>
Registry::try_get_type_exact(std::string_view name) {
    auto it = m_type_ids_by_name.find(std::string {name});
    if (it == m_type_ids_by_name.end()) {
        return failure(
            RegistryError::type_not_found(TypeId(name), std::string(name))
        );
    }

    return try_get_type(it->second);
}

Result<Type&, RegistryError> Registry::try_get_type(std::string_view name) {
    if (auto exact = try_get_type_exact(name)) {
        return *exact;
    }

    Type* match = nullptr;
    for (auto& [_, type] : m_types) {
        if (type.stripped_name() == name) {
            if (match) {
                return failure(
                    RegistryError::ambiguous_type_name(std::string {name})
                );
            }
            match = &type;
        }
    }

    if (match) {
        return *match;
    }

    return failure(
        RegistryError::type_not_found(TypeId(name), std::string(name))
    );
}

Cls& Registry::add_cls(TypeId id) {
    auto it = m_classes.find(id);
    if (it != m_classes.end()) {
        return it->second;
    }
    Cls cls(id);
    m_classes.emplace(cls.type_id(), std::move(cls));
    return m_classes.at(id);
}

Cls& Registry::get_cls(TypeId id) {
    auto result = try_get_cls(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Cls&, RegistryError> Registry::try_get_cls(TypeId id) {
    auto it = m_classes.find(id);
    if (it == m_classes.end()) {
        return failure(
            RegistryError::class_not_found(id, registered_type_name(id))
        );
    }
    return it->second;
}

Enum& Registry::add_enum(TypeId id) {
    auto it = m_enums.find(id);
    if (it != m_enums.end()) {
        return it->second;
    }
    Enum enum_info(id);
    m_enums.emplace(id, std::move(enum_info));
    return m_enums.at(id);
}

Enum& Registry::get_enum(TypeId id) {
    auto result = try_get_enum(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Enum&, RegistryError> Registry::try_get_enum(TypeId id) {
    auto it = m_enums.find(id);
    if (it == m_enums.end()) {
        return failure(
            RegistryError::enum_not_found(id, registered_type_name(id))
        );
    }
    return it->second;
}

GenericType&
Registry::register_generic_type(TypeId id, GenericType generic_type) {
    auto it = m_generic_types.find(id);
    if (it != m_generic_types.end()) {
        return it->second;
    }
    if (generic_type.specialized_type_id != id) {
        fatal(
            "Generic type id mismatch: registered id {} but generic type "
            "reports {}",
            id.id(),
            generic_type.specialized_type_id.id()
        );
    }
    auto inserted = m_generic_types.emplace(id, std::move(generic_type));
    return inserted.first->second;
}

GenericType& Registry::get_generic_type(TypeId id) {
    auto result = try_get_generic_type(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<GenericType&, RegistryError> Registry::try_get_generic_type(TypeId id) {
    auto it = m_generic_types.find(id);
    if (it == m_generic_types.end()) {
        return failure(
            RegistryError::generic_type_not_found(id, registered_type_name(id))
        );
    }
    return it->second;
}

ContainerAdapter& Registry::register_container_adapter(
    TypeId id,
    std::unique_ptr<ContainerAdapter> adapter
) {
    auto it = m_container_adapters.find(id);
    if (it != m_container_adapters.end()) {
        register_container_methods(add_cls(id), *it->second);
        return *it->second;
    }
    if (!adapter) {
        fatal("Cannot register null container adapter for id {}", id.id());
    }
    if (adapter->container_type() != id) {
        fatal(
            "Container adapter type id mismatch: registered id {} but adapter "
            "reports {}",
            id.id(),
            adapter->container_type().id()
        );
    }
    auto inserted = m_container_adapters.emplace(id, std::move(adapter));
    auto& registered = *inserted.first->second;
    register_container_methods(add_cls(id), registered);
    return registered;
}

ContainerAdapter& Registry::get_container_adapter(TypeId id) {
    auto result = try_get_container_adapter(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<ContainerAdapter&, RegistryError>
Registry::try_get_container_adapter(TypeId id) {
    auto it = m_container_adapters.find(id);
    if (it == m_container_adapters.end()) {
        return failure(
            RegistryError::container_adapter_not_found(
                id,
                registered_type_name(id)
            )
        );
    }
    return *it->second;
}

Type& Registry::add_generated_annotation(
    TypeId type_id,
    std::string annotation
) {
    auto& registered_type = get_type(type_id);
    auto& stored = registered_type.add_annotation(std::move(annotation));
    bind_generated_annotation(registered_type, stored);
    return registered_type;
}

Type& Registry::add_generated_annotation_field(
    TypeId type_id,
    std::string annotation,
    std::string field,
    std::string value
) {
    const std::string annotation_name = annotation;
    auto& registered_type = add_generated_annotation(type_id, annotation);
    registered_type.set_annotation_field(
        std::move(annotation),
        std::move(field),
        std::move(value)
    );
    auto position = std::ranges::lower_bound(
        registered_type.m_annotations,
        annotation_name,
        {},
        &Annotation::name
    );
    bind_generated_annotation(registered_type, *position);
    return registered_type;
}

void Registry::register_generated_annotation_schema(
    std::string name,
    TypeId schema_type,
    std::vector<std::string> fields,
    GeneratedAnnotationFactory factory,
    GeneratedAnnotationEncoder encoder
) {
    std::ranges::sort(fields);
    if (std::ranges::adjacent_find(fields) != fields.end()) {
        fatal("Annotation schema '{}' contains duplicate fields", name);
    }

    auto existing = m_annotation_schemas.find(name);
    if (existing != m_annotation_schemas.end()) {
        if (existing->second.type != schema_type ||
            existing->second.fields != fields) {
            fatal("Annotation schema '{}' has conflicting registrations", name);
        }
        return;
    }
    for (const auto& [registered_name, schema] : m_annotation_schemas) {
        if (schema.type == schema_type) {
            fatal(
                "Annotation schema type '{}' is registered as both '{}' and "
                "'{}'",
                registered_type_name(schema_type).value_or("<unknown>"),
                registered_name,
                name
            );
        }
    }

    const bool inserted = m_annotation_schemas
                              .emplace(
                                  name,
                                  GeneratedAnnotationSchema {
                                      .type = schema_type,
                                      .fields = std::move(fields),
                                      .factory = std::move(factory),
                                      .encoder = std::move(encoder),
                                  }
                              )
                              .second;
    ETS_ASSERT(inserted);

    for (auto& [_, reflected_type] : m_types) {
        auto annotation = std::ranges::lower_bound(
            reflected_type.m_annotations,
            name,
            {},
            &Annotation::name
        );
        if (annotation != reflected_type.m_annotations.end() &&
            annotation->name == name) {
            bind_generated_annotation(reflected_type, *annotation);
        }
    }
}

Type& Registry::add_typed_annotation(
    TypeId type_id,
    TypeId schema_type,
    std::shared_ptr<const void> payload
) {
    auto schema = m_annotation_schemas.end();
    for (auto candidate = m_annotation_schemas.begin();
         candidate != m_annotation_schemas.end();
         ++candidate) {
        if (candidate->second.type == schema_type) {
            schema = candidate;
            break;
        }
    }
    if (schema == m_annotation_schemas.end()) {
        fatal(
            "Annotation schema type '{}' is not registered",
            registered_type_name(schema_type).value_or("<unknown>")
        );
    }

    auto fields = schema->second.encoder(payload.get());
    std::ranges::sort(fields, {}, &AnnotationField::name);
    auto& reflected_type = get_type(type_id);
    auto& annotation = reflected_type.add_annotation(schema->first);
    annotation.fields = std::move(fields);
    annotation.schema_type = schema_type;
    annotation.payload = std::move(payload);
    bind_generated_annotation(reflected_type, annotation);
    return reflected_type;
}

void Registry::validate_generated_annotations() const {
    for (const auto& [_, reflected_type] : m_types) {
        for (const auto& annotation : reflected_type.m_annotations) {
            const auto schema = m_annotation_schemas.find(annotation.name);
            if (schema == m_annotation_schemas.end()) {
                fatal(
                    "Annotation '{}' on type '{}' has no registered schema",
                    annotation.name,
                    reflected_type.name()
                );
            }
            if (annotation.schema_type != schema->second.type ||
                !annotation.payload) {
                fatal(
                    "Annotation '{}' on type '{}' is not bound to its schema",
                    annotation.name,
                    reflected_type.name()
                );
            }
        }
    }
}

void Registry::bind_generated_annotation(Type& type, Annotation& annotation) {
    const auto schema = m_annotation_schemas.find(annotation.name);
    if (schema == m_annotation_schemas.end()) {
        return;
    }

    for (const auto& field : annotation.fields) {
        if (!std::ranges::binary_search(schema->second.fields, field.name)) {
            fatal(
                "Unknown field '{}' for annotation '{}' on type '{}'",
                field.name,
                annotation.name,
                type.name()
            );
        }
    }

    try {
        type.set_annotation_payload(
            annotation.name,
            schema->second.type,
            schema->second.factory(
                AnnotationView {annotation.name, annotation.fields}
            )
        );
    } catch (const std::exception& error) {
        fatal(
            "Invalid annotation '{}' on type '{}': {}",
            annotation.name,
            type.name(),
            error.what()
        );
    }
}

std::vector<TypeId>
Registry::types_with_annotation(std::string_view annotation) const {
    std::vector<TypeId> result;
    for (const auto& [id, reflected_type] : m_types) {
        if (reflected_type.has_annotation(annotation)) {
            result.push_back(id);
        }
    }
    std::ranges::sort(result);
    return result;
}

bool Registry::has_enum(TypeId id) const {
    return m_enums.contains(id);
}

void Registry::clear_generated_metadata() {
    ++m_class_epoch;
    m_classes.clear();
    m_enums.clear();
    m_annotation_schemas.clear();
    for (auto& [_, reflected_type] : m_types) {
        reflected_type.clear_annotations();
    }

    for (const auto& [id, adapter] : m_container_adapters) {
        register_container_methods(add_cls(id), *adapter);
    }
}

Type& type(TypeId id) {
    return Registry::instance().get_type(id);
}

const std::string& type_name(TypeId id) {
    return Registry::instance().get_type(id).name();
}

bool is_enum_type(TypeId type_id) {
    return Registry::instance().has_enum(type_id);
}

} // namespace ets
