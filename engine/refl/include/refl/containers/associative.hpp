#pragma once

#include "refl/container_adapter.hpp"
#include "refl/containers/detail.hpp"

#include <cstddef>
#include <utility>

namespace fei {

template<class Container, class Key, class Mapped>
class MapContainerAdapter final : public AssociativeContainerAdapter {
  public:
    using Element = typename Container::value_type;

    ContainerKind kind() const override { return ContainerKind::Map; }

    TypeId container_type() const override { return fei::type_id<Container>(); }

    TypeId key_type() const override { return fei::type_id<Key>(); }

    TypeId mapped_type() const override { return fei::type_id<Mapped>(); }

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

    Status<ContainerError> for_each_entry(
        Ref container,
        const ContainerEntryVisitor& visitor
    ) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "for_each_entry"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }

        if (container.is_const()) {
            std::size_t index = 0;
            for (auto it = (*result)->begin(); it != (*result)->end(); ++it) {
                auto status = visitor(
                    AssociativeElementRef {
                        .key = Ref(&it->first, fei::type_id<Key>()),
                        .value = Ref(&it->second, fei::type_id<Mapped>()),
                    },
                    index
                );
                if (!status) {
                    return failure(std::move(status.error()));
                }
                ++index;
            }
            return {};
        }

        auto mutable_result = detail::mutable_container<Container>(
            container,
            container_type(),
            "for_each_entry"
        );
        if (!mutable_result) {
            return failure(std::move(mutable_result.error()));
        }

        std::size_t index = 0;
        for (auto it = (*mutable_result)->begin();
             it != (*mutable_result)->end();
             ++it) {
            auto status = visitor(
                AssociativeElementRef {
                    .key = Ref(&it->first, fei::type_id<Key>()),
                    .value = Ref(&it->second, fei::type_id<Mapped>()),
                },
                index
            );
            if (!status) {
                return failure(std::move(status.error()));
            }
            ++index;
        }
        return {};
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

    Status<ContainerError>
    insert(Ref container, AssociativeElementRef entry) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "insert"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }

        if (!entry.key || entry.key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to map insert"
                )
            );
        }

        auto insert_with_key = [&](auto&& key_arg) -> Status<ContainerError> {
            return detail::consume_ref_value<Mapped>(
                entry.value,
                container_type(),
                "insert",
                [&](auto&& mapped) -> Status<ContainerError> {
                    if constexpr (requires {
                                      (*result)->insert_or_assign(
                                          std::forward<decltype(key_arg)>(
                                              key_arg
                                          ),
                                          std::forward<decltype(mapped)>(mapped)
                                      );
                                  }) {
                        (*result)->insert_or_assign(
                            std::forward<decltype(key_arg)>(key_arg),
                            std::forward<decltype(mapped)>(mapped)
                        );
                        return {};
                    }
                    return failure(
                        detail::container_error(
                            ContainerError::Kind::UnsupportedOperation,
                            container_type(),
                            "Cannot insert map value because key or mapped "
                            "type is not insertable"
                        )
                    );
                }
            );
        };

        return detail::consume_ref_value<Key>(
            entry.key,
            container_type(),
            "insert",
            [&](auto&& key_value) {
                return insert_with_key(
                    std::forward<decltype(key_value)>(key_value)
                );
            }
        );
    }

    Result<Ref, ContainerError> find(Ref container, Ref key) const override {
        if (container.is_const()) {
            auto result = detail::const_container<Container>(
                container,
                container_type(),
                "find"
            );
            if (!result) {
                return failure(std::move(result.error()));
            }
            if (!key || key.type_id() != fei::type_id<Key>()) {
                return failure(
                    detail::container_error(
                        ContainerError::Kind::InvalidElement,
                        container_type(),
                        "Invalid key passed to map find"
                    )
                );
            }
            auto it = (*result)->find(key.get_const<Key>());
            if (it == (*result)->end()) {
                return failure(
                    detail::container_error(
                        ContainerError::Kind::NotFound,
                        container_type(),
                        "Map key was not found"
                    )
                );
            }
            return Ref(&it->second, fei::type_id<Mapped>());
        }

        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "find"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!key || key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to map find"
                )
            );
        }
        auto it = (*result)->find(key.get_const<Key>());
        if (it == (*result)->end()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::NotFound,
                    container_type(),
                    "Map key was not found"
                )
            );
        }
        return Ref(&it->second, fei::type_id<Mapped>());
    }

    Result<bool, ContainerError>
    contains(Ref container, Ref key) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "contains"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!key || key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to map contains"
                )
            );
        }
        return (*result)->find(key.get_const<Key>()) != (*result)->end();
    }

    Status<ContainerError> erase(Ref container, Ref key) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "erase"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!key || key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to map erase"
                )
            );
        }
        auto it = (*result)->find(key.get_const<Key>());
        if (it == (*result)->end()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::NotFound,
                    container_type(),
                    "Map key was not found"
                )
            );
        }
        (*result)->erase(it);
        return {};
    }
};

template<class Container, class Key>
class SetContainerAdapter final : public AssociativeContainerAdapter {
  public:
    using Element = typename Container::value_type;

    ContainerKind kind() const override { return ContainerKind::Set; }

    TypeId container_type() const override { return fei::type_id<Container>(); }

    TypeId key_type() const override { return fei::type_id<Key>(); }

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

    Status<ContainerError> for_each_entry(
        Ref container,
        const ContainerEntryVisitor& visitor
    ) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "for_each_entry"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }

        std::size_t index = 0;
        for (auto it = (*result)->begin(); it != (*result)->end(); ++it) {
            auto status = visitor(
                AssociativeElementRef {
                    .key = Ref(&*it, fei::type_id<Key>()),
                    .value = Ref(),
                },
                index
            );
            if (!status) {
                return failure(std::move(status.error()));
            }
            ++index;
        }
        return {};
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

    Status<ContainerError>
    insert(Ref container, AssociativeElementRef entry) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "insert"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }

        if (!entry.key || entry.key.type_id() != fei::type_id<Key>() ||
            entry.value) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid entry passed to set insert"
                )
            );
        }

        return detail::consume_ref_value<Key>(
            entry.key,
            container_type(),
            "insert",
            [&](auto&& key_value) -> Status<ContainerError> {
                if constexpr (requires {
                                  (*result)->insert(
                                      std::forward<decltype(key_value)>(
                                          key_value
                                      )
                                  );
                              }) {
                    (*result)->insert(
                        std::forward<decltype(key_value)>(key_value)
                    );
                    return {};
                }
                return failure(
                    detail::container_error(
                        ContainerError::Kind::UnsupportedOperation,
                        container_type(),
                        "Cannot insert set key because key type is not "
                        "insertable"
                    )
                );
            }
        );
    }

    Result<Ref, ContainerError> find(Ref container, Ref key) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "find"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!key || key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to set find"
                )
            );
        }
        auto it = (*result)->find(key.get_const<Key>());
        if (it == (*result)->end()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::NotFound,
                    container_type(),
                    "Set key was not found"
                )
            );
        }
        return Ref(&*it, fei::type_id<Key>());
    }

    Result<bool, ContainerError>
    contains(Ref container, Ref key) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "contains"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!key || key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to set contains"
                )
            );
        }
        return (*result)->find(key.get_const<Key>()) != (*result)->end();
    }

    Status<ContainerError> erase(Ref container, Ref key) const override {
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "erase"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        if (!key || key.type_id() != fei::type_id<Key>()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::InvalidElement,
                    container_type(),
                    "Invalid key passed to set erase"
                )
            );
        }
        auto it = (*result)->find(key.get_const<Key>());
        if (it == (*result)->end()) {
            return failure(
                detail::container_error(
                    ContainerError::Kind::NotFound,
                    container_type(),
                    "Set key was not found"
                )
            );
        }
        (*result)->erase(it);
        return {};
    }
};

} // namespace fei
