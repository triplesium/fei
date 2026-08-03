#pragma once

#include "refl/container_adapter.hpp"
#include "refl/containers/detail.hpp"

#include <cstddef>
#include <utility>

namespace fei {

template<class Container, class Element>
class VectorContainerAdapter final : public IndexedContainerAdapter {
  public:
    ContainerKind kind() const override { return ContainerKind::Sequence; }

    TypeId container_type() const override { return fei::type_id<Container>(); }

    TypeId element_type() const override { return fei::type_id<Element>(); }

    Result<std::size_t, ContainerError> size(Ref container) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "size"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        return (*result)->size();
    }

    bool fixed_size() const override { return false; }

    Status<ContainerError> for_each(
        Ref container,
        const ContainerElementVisitor& visitor
    ) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (container.is_const()) {
            return detail::for_each_ref(container, **result, visitor);
        }

        auto mutable_result = detail::mutable_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!mutable_result) {
            return failure(std::move(mutable_result.error()));
        }
        return detail::for_each_ref(container, **mutable_result, visitor);
    }

    Result<Ref, ContainerError>
    at(Ref container, std::size_t index) const override {
        if (container.is_const()) {
            auto result = detail::const_container<Container>(
                container,
                container_type(),
                "at"
            );
            if (!result) {
                return failure(std::move(result.error()));
            }
            if (index >= (*result)->size()) {
                return failure(
                    detail::container_error(
                        ContainerError::Kind::OutOfRange,
                        container_type(),
                        "Sequence element index is out of range"
                    )
                );
            }
            return detail::element_ref(container, (*result)->at(index));
        }

        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "at"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index >= (*result)->size()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Sequence element index is out of range"
                )
            );
        }
        return detail::element_ref(container, (*result)->at(index));
    }

    Status<ContainerError>
    assign(Ref container, std::size_t index, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "assign"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index >= (*result)->size()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Sequence element index is out of range"
                )
            );
        }
        return detail::assign_ref_value<Element>(
            detail::element_ref(container, (*result)->at(index)),
            value,
            container_type(),
            "assign"
        );
    }

    Status<ContainerError> clear(Ref container) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "clear"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        (*result)->clear();
        return {};
    }

    Status<ContainerError> append(Ref container, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "append"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }

        return detail::consume_ref_value<Element>(
            value,
            container_type(),
            "append",
            [&](auto&& src) -> Status<ContainerError> {
                if constexpr (requires {
                                  (*result)->push_back(
                                      std::forward<decltype(src)>(src)
                                  );
                              }) {
                    (*result)->push_back(std::forward<decltype(src)>(src));
                    return {};
                }
                return failure(
                    detail::container_error(
                        ContainerError::Kind::UnsupportedOperation,
                        container_type(),
                        "Cannot append value because element type is not "
                        "insertable"
                    )
                );
            }
        );
    }

    Status<ContainerError>
    insert(Ref container, std::size_t index, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "insert"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index > (*result)->size()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Sequence insertion index is out of range"
                )
            );
        }

        return detail::consume_ref_value<Element>(
            value,
            container_type(),
            "insert",
            [&](auto&& src) -> Status<ContainerError> {
                const auto position =
                    (*result)->begin() +
                    static_cast<typename Container::difference_type>(index);
                if constexpr (requires {
                                  (*result)->insert(
                                      position,
                                      std::forward<decltype(src)>(src)
                                  );
                              }) {
                    (*result)->insert(
                        position,
                        std::forward<decltype(src)>(src)
                    );
                    return {};
                }
                return failure(
                    detail::container_error(
                        ContainerError::Kind::UnsupportedOperation,
                        container_type(),
                        "Cannot insert value because element type is not "
                        "insertable"
                    )
                );
            }
        );
    }

    Status<ContainerError>
    erase(Ref container, std::size_t index) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "erase"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index >= (*result)->size()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Sequence erase index is out of range"
                )
            );
        }
        const auto position =
            (*result)->begin() +
            static_cast<typename Container::difference_type>(index);
        (*result)->erase(position);
        return {};
    }
};

template<class Container, class Element, std::size_t Size>
class ArrayContainerAdapter final : public IndexedContainerAdapter {
  public:
    ContainerKind kind() const override { return ContainerKind::Sequence; }

    TypeId container_type() const override { return fei::type_id<Container>(); }

    TypeId element_type() const override { return fei::type_id<Element>(); }

    Result<std::size_t, ContainerError> size(Ref container) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "size"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        return Size;
    }

    bool fixed_size() const override { return true; }

    Status<ContainerError> for_each(
        Ref container,
        const ContainerElementVisitor& visitor
    ) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (container.is_const()) {
            return detail::for_each_ref(container, **result, visitor);
        }

        auto mutable_result = detail::mutable_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!mutable_result) {
            return failure(std::move(mutable_result.error()));
        }
        return detail::for_each_ref(container, **mutable_result, visitor);
    }

    Result<Ref, ContainerError>
    at(Ref container, std::size_t index) const override {
        if (container.is_const()) {
            auto result = detail::const_container<Container>(
                container,
                container_type(),
                "at"
            );
            if (!result) {
                return failure(std::move(result.error()));
            }
            if (index >= Size) {
                return failure(
                    detail::container_error(
                        ContainerError::Kind::OutOfRange,
                        container_type(),
                        "Array element index is out of range"
                    )
                );
            }
            return detail::element_ref(container, (*result)->at(index));
        }

        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "at"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index >= Size) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Array element index is out of range"
                )
            );
        }
        return detail::element_ref(container, (*result)->at(index));
    }

    Status<ContainerError>
    assign(Ref container, std::size_t index, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "assign"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index >= Size) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Array element index is out of range"
                )
            );
        }
        return detail::assign_ref_value<Element>(
            detail::element_ref(container, (*result)->at(index)),
            value,
            container_type(),
            "assign"
        );
    }
};

template<class Container, class Value>
class OptionalContainerAdapter final : public IndexedContainerAdapter {
  public:
    ContainerKind kind() const override { return ContainerKind::Optional; }

    TypeId container_type() const override { return fei::type_id<Container>(); }

    TypeId element_type() const override { return fei::type_id<Value>(); }

    Result<std::size_t, ContainerError> size(Ref container) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "size"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        return (*result)->has_value() ? 1U : 0U;
    }

    bool fixed_size() const override { return false; }

    Status<ContainerError> for_each(
        Ref container,
        const ContainerElementVisitor& visitor
    ) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!(*result)->has_value()) {
            return {};
        }
        if (container.is_const()) {
            return visitor(
                detail::element_ref(container, (*result)->value()),
                0
            );
        }

        auto mutable_result = detail::mutable_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!mutable_result) {
            return failure(std::move(mutable_result.error()));
        }
        return visitor(
            detail::element_ref(container, (*mutable_result)->value()),
            0
        );
    }

    Result<Ref, ContainerError>
    at(Ref container, std::size_t index) const override {
        if (container.is_const()) {
            auto result = detail::const_container<Container>(
                container,
                container_type(),
                "at"
            );
            if (!result) {
                return failure(std::move(result.error()));
            }
            if (index != 0 || !(*result)->has_value()) {
                return failure(
                    detail::container_error(
                        ContainerError::Kind::OutOfRange,
                        container_type(),
                        "Optional value is empty or index is out of range"
                    )
                );
            }
            return detail::element_ref(container, (*result)->value());
        }

        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "at"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index != 0 || !(*result)->has_value()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Optional value is empty or index is out of range"
                )
            );
        }
        return detail::element_ref(container, (*result)->value());
    }

    Status<ContainerError>
    assign(Ref container, std::size_t index, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "assign"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index != 0 || !(*result)->has_value()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Optional value is empty or index is out of range"
                )
            );
        }
        return detail::assign_ref_value<Value>(
            detail::element_ref(container, (*result)->value()),
            value,
            container_type(),
            "assign"
        );
    }

    Status<ContainerError> clear(Ref container) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "clear"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        (*result)->reset();
        return {};
    }

    Status<ContainerError> append(Ref container, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "append"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if ((*result)->has_value()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Optional already contains a value; use assign to replace "
                    "it"
                )
            );
        }

        return detail::consume_ref_value<Value>(
            value,
            container_type(),
            "append",
            [&](auto&& src) -> Status<ContainerError> {
                if constexpr (requires {
                                  (*result)->emplace(
                                      std::forward<decltype(src)>(src)
                                  );
                              }) {
                    (*result)->emplace(std::forward<decltype(src)>(src));
                    return {};
                }
                return failure(
                    detail::container_error(
                        ContainerError::Kind::UnsupportedOperation,
                        container_type(),
                        "Cannot append value because optional value type is "
                        "not "
                        "constructible"
                    )
                );
            }
        );
    }

    Status<ContainerError>
    insert(Ref container, std::size_t index, Ref value) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "insert"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index != 0) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Optional insertion index is out of range"
                )
            );
        }
        return append(container, value);
    }

    Status<ContainerError>
    erase(Ref container, std::size_t index) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "erase"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (index != 0 || !(*result)->has_value()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::OutOfRange,
                    container_type(),
                    "Optional value is empty or index is out of range"
                )
            );
        }
        (*result)->reset();
        return {};
    }
};

} // namespace fei
