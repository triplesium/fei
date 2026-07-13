#include "devtools/schema.hpp"

#include "devtools/types.hpp"
#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/dynamic_array.hpp"
#include "refl/dynamic_map.hpp"
#include "refl/enum.hpp"
#include "refl/generic_type.hpp"
#include "refl/property.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei::devtools {
namespace {

using Json = nlohmann::json;

template<class T>
bool same_type(TypeId type) {
    return type == type_id<T>();
}

bool is_signed_integer(TypeId type) {
    return same_type<char>(type) || same_type<signed char>(type) ||
           same_type<short int>(type) || same_type<int>(type) ||
           same_type<long int>(type) || same_type<long long int>(type);
}

bool is_unsigned_integer(TypeId type) {
    return same_type<unsigned char>(type) ||
           same_type<unsigned short int>(type) ||
           same_type<unsigned int>(type) ||
           same_type<unsigned long int>(type) ||
           same_type<unsigned long long int>(type);
}

bool is_floating(TypeId type) {
    return same_type<float>(type) || same_type<double>(type) ||
           same_type<long double>(type);
}

const char* container_kind_name(ContainerKind kind) {
    switch (kind) {
        case ContainerKind::Sequence:
            return "sequence";
        case ContainerKind::Optional:
            return "optional";
        case ContainerKind::Product:
            return "product";
        case ContainerKind::Map:
            return "map";
        case ContainerKind::Set:
            return "set";
    }
    return "unsupported";
}

class SchemaBuilder {
  private:
    Registry& m_registry {Registry::instance()};
    std::unordered_set<TypeId> m_visited;
    Json m_types {Json::object()};

    Result<std::string, std::string> add_type_reference(TypeId id) {
        auto status = add_type(id);
        if (!status) {
            return failure(std::move(status.error()));
        }
        auto type = m_registry.try_get_type(id);
        if (!type) {
            return failure(type.error().message);
        }
        return type->name();
    }

    Status<std::string> add_enum(TypeId id, Json& schema) {
        auto reflected = m_registry.try_get_enum(id);
        if (!reflected) {
            return failure(reflected.error().message);
        }

        std::vector<std::pair<std::string, std::int64_t>> values(
            reflected->enumerators().begin(),
            reflected->enumerators().end()
        );
        std::ranges::sort(
            values,
            {},
            &std::pair<std::string, std::int64_t>::first
        );
        Json enumerators = Json::array();
        for (const auto& [name, value] : values) {
            enumerators.push_back(Json {{"name", name}, {"value", value}});
        }
        schema["kind"] = "enum";
        schema["values"] = std::move(enumerators);
        return {};
    }

    Status<std::string> add_object(TypeId id, Json& schema) {
        auto reflected = m_registry.try_get_cls(id);
        if (!reflected) {
            return failure(reflected.error().message);
        }

        auto properties = reflected->get_properties();
        std::ranges::sort(
            properties,
            [](const Property* lhs, const Property* rhs) {
                return lhs->name() < rhs->name();
            }
        );
        Json fields = Json::array();
        for (const auto* property : properties) {
            auto type_name = add_type_reference(property->type_id());
            if (!type_name) {
                return failure(std::move(type_name.error()));
            }
            fields.push_back(
                Json {
                    {"name", property->name()},
                    {"type", std::move(*type_name)},
                    {"required", true},
                }
            );
        }
        schema["kind"] = "object";
        schema["properties"] = std::move(fields);
        return {};
    }

    Status<std::string> add_generic_arguments(TypeId id, Json& schema) {
        auto generic = m_registry.try_get_generic_type(id);
        if (!generic) {
            return {};
        }

        schema["generic"] = generic->generic_name;
        Json arguments = Json::array();
        for (const auto& argument : generic->arguments) {
            switch (argument.kind) {
                case GenericArgument::Kind::Type: {
                    auto type_name = add_type_reference(argument.type_id);
                    if (!type_name) {
                        return failure(std::move(type_name.error()));
                    }
                    arguments.push_back(
                        Json {{"kind", "type"}, {"type", std::move(*type_name)}}
                    );
                    break;
                }
                case GenericArgument::Kind::SignedInteger:
                    arguments.push_back(
                        Json {
                            {"kind", "signed_integer"},
                            {"value", argument.signed_integer},
                        }
                    );
                    break;
                case GenericArgument::Kind::UnsignedInteger:
                    arguments.push_back(
                        Json {
                            {"kind", "unsigned_integer"},
                            {"value", argument.unsigned_integer},
                        }
                    );
                    break;
            }
        }
        schema["arguments"] = std::move(arguments);
        return {};
    }

    Status<std::string>
    add_container(TypeId id, ContainerAdapter& container, Json& schema) {
        schema["kind"] = container_kind_name(container.kind());

        if (auto* associative = container.associative()) {
            auto key_type = add_type_reference(associative->key_type());
            if (!key_type) {
                return failure(std::move(key_type.error()));
            }
            schema["key_type"] = std::move(*key_type);
            if (associative->has_mapped_value()) {
                auto mapped_type =
                    add_type_reference(associative->mapped_type());
                if (!mapped_type) {
                    return failure(std::move(mapped_type.error()));
                }
                schema["mapped_type"] = std::move(*mapped_type);
            }
            return add_generic_arguments(id, schema);
        }

        auto* indexed = container.indexed();
        if (!indexed) {
            schema["kind"] = "unsupported";
            return {};
        }
        schema["fixed_size"] = indexed->fixed_size();
        if (container.kind() != ContainerKind::Product) {
            auto element_type = indexed->element_type();
            if (element_type) {
                auto type_name = add_type_reference(element_type);
                if (!type_name) {
                    return failure(std::move(type_name.error()));
                }
                schema["element_type"] = std::move(*type_name);
            }
        }
        return add_generic_arguments(id, schema);
    }

    Status<std::string> add_type(TypeId id) {
        if (!id || m_visited.contains(id)) {
            return {};
        }

        auto type = m_registry.try_get_type(id);
        if (!type) {
            return failure(type.error().message);
        }
        m_visited.insert(id);

        Json schema {{"name", type->name()}};
        Status<std::string> status;
        if (same_type<bool>(id)) {
            schema["kind"] = "bool";
        } else if (same_type<std::string>(id)) {
            schema["kind"] = "string";
        } else if (is_signed_integer(id)) {
            schema["kind"] = "signed_integer";
        } else if (is_unsigned_integer(id)) {
            schema["kind"] = "unsigned_integer";
        } else if (is_floating(id)) {
            schema["kind"] = "floating";
        } else if (same_type<BlobRef>(id)) {
            schema["kind"] = "blob_ref";
        } else if (same_type<DynamicArray>(id)) {
            schema["kind"] = "dynamic_array";
            schema["runtime_schema"] = true;
        } else if (same_type<DynamicMap>(id)) {
            schema["kind"] = "dynamic_map";
            schema["runtime_schema"] = true;
        } else if (m_registry.has_enum(id)) {
            status = add_enum(id, schema);
        } else if (auto container = m_registry.try_get_container_adapter(id)) {
            status = add_container(id, *container, schema);
        } else if (auto reflected = m_registry.try_get_cls(id)) {
            (void)reflected;
            status = add_object(id, schema);
        } else {
            schema["kind"] = "unsupported";
            status = add_generic_arguments(id, schema);
        }
        if (!status) {
            return failure(std::move(status.error()));
        }

        m_types[type->name()] = std::move(schema);
        return {};
    }

  public:
    Result<std::string, std::string> build(const std::vector<TypeId>& roots) {
        std::vector<std::string> root_names;
        root_names.reserve(roots.size());
        for (auto root : roots) {
            if (!root) {
                continue;
            }
            auto name = add_type_reference(root);
            if (!name) {
                return failure(std::move(name.error()));
            }
            root_names.push_back(std::move(*name));
        }
        std::ranges::sort(root_names);
        root_names.erase(
            std::ranges::unique(root_names).begin(),
            root_names.end()
        );
        return Json {
            {"version", 1},
            {"roots", std::move(root_names)},
            {"types", std::move(m_types)},
        }
            .dump();
    }
};

} // namespace

Result<std::string, std::string>
build_schema_json(const std::vector<TypeId>& roots) {
    return SchemaBuilder {}.build(roots);
}

} // namespace fei::devtools
