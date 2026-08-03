#include "dynamic_container_adapter.hpp"

#include "refl/container_adapter.hpp"
#include "refl/containers/detail.hpp"
#include "refl/dynamic_array.hpp"
#include "refl/dynamic_map.hpp"
#include "refl/registry.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace fei {
namespace {

ContainerError adapt_error(DynamicArrayError error) {
    auto kind = ContainerError::Kind::InvalidContainer;
    switch (error.kind) {
        case DynamicArrayError::Kind::InvalidElementType:
        case DynamicArrayError::Kind::ElementTypeNotFound:
            kind = ContainerError::Kind::InvalidContainer;
            break;
        case DynamicArrayError::Kind::EmptyValue:
        case DynamicArrayError::Kind::TypeMismatch:
        case DynamicArrayError::Kind::ValueNotStorable:
            kind = ContainerError::Kind::InvalidElement;
            break;
        case DynamicArrayError::Kind::OutOfRange:
            kind = ContainerError::Kind::OutOfRange;
            break;
    }
    return ContainerError::make(
        kind,
        type_id<DynamicArray>(),
        std::move(error.message)
    );
}

ContainerError adapt_error(DynamicMapError error) {
    auto kind = ContainerError::Kind::InvalidElement;
    switch (error.kind) {
        case DynamicMapError::Kind::InvalidKeyType:
        case DynamicMapError::Kind::InvalidMappedType:
        case DynamicMapError::Kind::KeyTypeNotFound:
        case DynamicMapError::Kind::MappedTypeNotFound:
            kind = ContainerError::Kind::InvalidContainer;
            break;
        case DynamicMapError::Kind::KeyNotFound:
            kind = ContainerError::Kind::NotFound;
            break;
        case DynamicMapError::Kind::KeyNotStorable:
        case DynamicMapError::Kind::KeyNotComparable:
        case DynamicMapError::Kind::KeyNotHashable:
        case DynamicMapError::Kind::InvalidKeyValue:
        case DynamicMapError::Kind::MappedValueNotStorable:
        case DynamicMapError::Kind::EmptyKey:
        case DynamicMapError::Kind::EmptyMappedValue:
        case DynamicMapError::Kind::KeyTypeMismatch:
        case DynamicMapError::Kind::MappedTypeMismatch:
        case DynamicMapError::Kind::InvalidVisitor:
        case DynamicMapError::Kind::VisitorFailed:
            kind = ContainerError::Kind::InvalidElement;
            break;
    }
    return ContainerError::make(
        kind,
        type_id<DynamicMap>(),
        std::move(error.message)
    );
}

template<class Array>
Status<ContainerError>
visit_array(Array& array, const ContainerElementVisitor& visitor) {
    for (std::size_t index = 0; index < array.size(); ++index) {
        auto element = array.at(index);
        if (!element) {
            return failure(adapt_error(std::move(element.error())));
        }
        auto status = visitor(*element, index);
        if (!status) {
            return failure(std::move(status.error()));
        }
    }
    return {};
}

class DynamicArrayContainerAdapter final : public IndexedContainerAdapter {
  public:
    ContainerKind kind() const override { return ContainerKind::Sequence; }

    TypeId container_type() const override { return type_id<DynamicArray>(); }

    TypeId element_type() const override { return {}; }

    Result<TypeId, ContainerError> element_type(Ref container) const override {
        auto array = detail::const_container<DynamicArray>(
            container,
            container_type(),
            "element_type"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        return (*array)->element_type();
    }

    Result<TypeId, ContainerError>
    element_type(Ref container, std::size_t index) const override {
        (void)index;
        return element_type(container);
    }

    Result<std::size_t, ContainerError> size(Ref container) const override {
        auto array = detail::const_container<DynamicArray>(
            container,
            container_type(),
            "size"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        return (*array)->size();
    }

    bool fixed_size() const override { return false; }

    Status<ContainerError> for_each(
        Ref container,
        const ContainerElementVisitor& visitor
    ) const override {
        if (!visitor) {
            return failure(
                ContainerError::make(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "DynamicArray visitor cannot be empty"
                )
            );
        }
        if (container.is_const()) {
            auto array = detail::const_container<DynamicArray>(
                container,
                container_type(),
                "for_each"
            );
            if (!array) {
                return failure(std::move(array.error()));
            }
            return visit_array(**array, visitor);
        }

        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "for_each"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        return visit_array(**array, visitor);
    }

    Result<Ref, ContainerError>
    at(Ref container, std::size_t index) const override {
        if (container.is_const()) {
            auto array = detail::const_container<DynamicArray>(
                container,
                container_type(),
                "at"
            );
            if (!array) {
                return failure(std::move(array.error()));
            }
            auto element = (*array)->at(index);
            if (!element) {
                return failure(adapt_error(std::move(element.error())));
            }
            return *element;
        }

        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "at"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        auto element = (*array)->at(index);
        if (!element) {
            return failure(adapt_error(std::move(element.error())));
        }
        return *element;
    }

    Status<ContainerError>
    assign(Ref container, std::size_t index, Ref value) const override {
        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "assign"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        auto status = (*array)->set(index, value);
        if (!status) {
            return failure(adapt_error(std::move(status.error())));
        }
        return {};
    }

    Status<ContainerError> clear(Ref container) const override {
        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "clear"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        (*array)->clear();
        return {};
    }

    Status<ContainerError> append(Ref container, Ref value) const override {
        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "append"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        auto status = (*array)->push(value);
        if (!status) {
            return failure(adapt_error(std::move(status.error())));
        }
        return {};
    }

    Status<ContainerError>
    insert(Ref container, std::size_t index, Ref value) const override {
        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "insert"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        auto status = (*array)->insert(index, value);
        if (!status) {
            return failure(adapt_error(std::move(status.error())));
        }
        return {};
    }

    Status<ContainerError>
    erase(Ref container, std::size_t index) const override {
        auto array = detail::mutable_container<DynamicArray>(
            container,
            container_type(),
            "erase"
        );
        if (!array) {
            return failure(std::move(array.error()));
        }
        auto status = (*array)->erase(index);
        if (!status) {
            return failure(adapt_error(std::move(status.error())));
        }
        return {};
    }
};

template<class Map>
Status<ContainerError>
visit_map_entries(Map& map, const ContainerEntryVisitor& visitor) {
    std::optional<ContainerError> visitor_error;
    auto status = map.for_each_entry(
        [&](DynamicMapEntryRef entry,
            std::size_t index) -> Status<DynamicMapError> {
            auto result = visitor(
                AssociativeElementRef {
                    .key = entry.key,
                    .value = entry.value,
                },
                index
            );
            if (result) {
                return {};
            }
            visitor_error = std::move(result.error());
            return failure(
                DynamicMapError {
                    .kind = DynamicMapError::Kind::VisitorFailed,
                    .message = visitor_error->message,
                }
            );
        }
    );
    if (visitor_error) {
        return failure(std::move(*visitor_error));
    }
    if (!status) {
        return failure(adapt_error(std::move(status.error())));
    }
    return {};
}

class DynamicMapContainerAdapter final : public AssociativeContainerAdapter {
  public:
    ContainerKind kind() const override { return ContainerKind::Map; }

    TypeId container_type() const override { return type_id<DynamicMap>(); }

    TypeId key_type() const override { return {}; }

    Result<TypeId, ContainerError> key_type(Ref container) const override {
        auto map = detail::const_container<DynamicMap>(
            container,
            container_type(),
            "key_type"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        return (*map)->key_type();
    }

    TypeId mapped_type() const override { return {}; }

    Result<TypeId, ContainerError> mapped_type(Ref container) const override {
        auto map = detail::const_container<DynamicMap>(
            container,
            container_type(),
            "mapped_type"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        return (*map)->mapped_type();
    }

    Result<std::size_t, ContainerError> size(Ref container) const override {
        auto map = detail::const_container<DynamicMap>(
            container,
            container_type(),
            "size"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        return (*map)->size();
    }

    Status<ContainerError> for_each_entry(
        Ref container,
        const ContainerEntryVisitor& visitor
    ) const override {
        if (!visitor) {
            return failure(
                ContainerError::make(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "DynamicMap entry visitor cannot be empty"
                )
            );
        }
        if (container.is_const()) {
            auto map = detail::const_container<DynamicMap>(
                container,
                container_type(),
                "for_each_entry"
            );
            if (!map) {
                return failure(std::move(map.error()));
            }
            return visit_map_entries(**map, visitor);
        }

        auto map = detail::mutable_container<DynamicMap>(
            container,
            container_type(),
            "for_each_entry"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        return visit_map_entries(**map, visitor);
    }

    Status<ContainerError> clear(Ref container) const override {
        auto map = detail::mutable_container<DynamicMap>(
            container,
            container_type(),
            "clear"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        (*map)->clear();
        return {};
    }

    Status<ContainerError>
    insert(Ref container, AssociativeElementRef entry) const override {
        auto map = detail::mutable_container<DynamicMap>(
            container,
            container_type(),
            "insert"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        auto status = (*map)->insert_or_assign(entry.key, entry.value);
        if (!status) {
            return failure(adapt_error(std::move(status.error())));
        }
        return {};
    }

    Result<Ref, ContainerError> find(Ref container, Ref key) const override {
        if (container.is_const()) {
            auto map = detail::const_container<DynamicMap>(
                container,
                container_type(),
                "find"
            );
            if (!map) {
                return failure(std::move(map.error()));
            }
            auto value = (*map)->find(key);
            if (!value) {
                return failure(adapt_error(std::move(value.error())));
            }
            return *value;
        }

        auto map = detail::mutable_container<DynamicMap>(
            container,
            container_type(),
            "find"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        auto value = (*map)->find(key);
        if (!value) {
            return failure(adapt_error(std::move(value.error())));
        }
        return *value;
    }

    Result<bool, ContainerError>
    contains(Ref container, Ref key) const override {
        auto map = detail::const_container<DynamicMap>(
            container,
            container_type(),
            "contains"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        auto value = (*map)->find(key);
        if (value) {
            return true;
        }
        if (value.error().kind == DynamicMapError::Kind::KeyNotFound) {
            return false;
        }
        return failure(adapt_error(std::move(value.error())));
    }

    Status<ContainerError> erase(Ref container, Ref key) const override {
        auto map = detail::mutable_container<DynamicMap>(
            container,
            container_type(),
            "erase"
        );
        if (!map) {
            return failure(std::move(map.error()));
        }
        auto status = (*map)->erase(key);
        if (!status) {
            return failure(adapt_error(std::move(status.error())));
        }
        return {};
    }
};

} // namespace

void register_dynamic_container_adapters(Registry& registry) {
    registry.register_type<DynamicArray>();
    registry.register_container_adapter(
        type_id<DynamicArray>(),
        std::make_unique<DynamicArrayContainerAdapter>()
    );

    registry.register_type<DynamicMap>();
    registry.register_container_adapter(
        type_id<DynamicMap>(),
        std::make_unique<DynamicMapContainerAdapter>()
    );
}

} // namespace fei
