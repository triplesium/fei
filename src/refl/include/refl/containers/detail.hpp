#pragma once

#include "refl/container_adapter.hpp"

#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fei {

namespace detail {

inline ContainerError container_error(
    ContainerError::Kind kind,
    TypeId container_type,
    std::string message
) {
    return ContainerError::make(kind, container_type, std::move(message));
}

template<class Container>
Result<const Container*, ContainerError>
const_container(Ref ref, TypeId expected_type, const char* operation) {
    if (!ref || ref.type_id() != expected_type) {
        return failure(container_error(
            ContainerError::Kind::InvalidContainer,
            expected_type,
            std::string("Invalid container passed to ") + operation
        ));
    }

    const auto* container = ref.try_get_const<Container>();
    if (!container) {
        return failure(container_error(
            ContainerError::Kind::InvalidContainer,
            expected_type,
            std::string("Invalid container passed to ") + operation
        ));
    }
    return container;
}

template<class Container>
Result<Container*, ContainerError>
mutable_container(Ref ref, TypeId expected_type, const char* operation) {
    if (!ref || ref.type_id() != expected_type || ref.is_const()) {
        return failure(container_error(
            ContainerError::Kind::InvalidContainer,
            expected_type,
            std::string("Invalid or const container passed to ") + operation
        ));
    }

    auto* container = ref.try_get<Container>();
    if (!container) {
        return failure(container_error(
            ContainerError::Kind::InvalidContainer,
            expected_type,
            std::string("Invalid or const container passed to ") + operation
        ));
    }
    return container;
}

template<class Element>
Ref element_ref(Ref owner, Element& value) {
    using Value = std::remove_cv_t<Element>;
    if constexpr (std::is_const_v<Element>) {
        (void)owner;
        return Ref(static_cast<const Value*>(&value), fei::type_id<Value>());
    } else {
        if (owner.is_const()) {
            return Ref(
                static_cast<const Value*>(&value),
                fei::type_id<Value>()
            );
        }
        return Ref(static_cast<Value*>(&value), fei::type_id<Value>());
    }
}

template<class Range>
Status<ContainerError>
for_each_ref(Ref owner, Range& range, const ContainerElementVisitor& visitor) {
    std::size_t index = 0;
    for (auto& value : range) {
        auto status = visitor(element_ref(owner, value), index);
        if (!status) {
            return failure(std::move(status.error()));
        }
        ++index;
    }
    return {};
}

template<class Value, class Func>
Status<ContainerError> consume_ref_value(
    Ref value,
    TypeId container_type,
    const char* operation,
    Func&& func
) {
    auto invoke = []<class Callable, class Arg>(Callable&& func, Arg&& arg)
        -> Status<ContainerError> {
        using ResultType = std::invoke_result_t<Callable, Arg>;
        if constexpr (std::same_as<ResultType, Status<ContainerError>>) {
            return std::forward<Callable>(func)(std::forward<Arg>(arg));
        } else {
            std::forward<Callable>(func)(std::forward<Arg>(arg));
            return {};
        }
    };

    if (!value || value.type_id() != fei::type_id<Value>()) {
        return failure(container_error(
            ContainerError::Kind::InvalidElement,
            container_type,
            std::string("Invalid value passed to ") + operation
        ));
    }

    if constexpr (std::copy_constructible<Value>) {
        return invoke(std::forward<Func>(func), value.get_const<Value>());
    } else if constexpr (std::move_constructible<Value>) {
        if (value.is_const()) {
            return failure(container_error(
                ContainerError::Kind::InvalidElement,
                container_type,
                std::string("Cannot move from const value passed to ") +
                    operation
            ));
        }
        return invoke(std::forward<Func>(func), std::move(value.get<Value>()));
    } else {
        return failure(container_error(
            ContainerError::Kind::InvalidElement,
            container_type,
            std::string("Value type cannot be copied or moved for ") + operation
        ));
    }
}

template<class Element>
Status<ContainerError> assign_ref_value(
    Ref target,
    Ref value,
    TypeId container_type,
    const char* operation
) {
    using Value = std::remove_cv_t<Element>;
    if (!target || target.type_id() != fei::type_id<Value>() ||
        target.is_const()) {
        return failure(container_error(
            ContainerError::Kind::InvalidElement,
            container_type,
            std::string("Invalid assignment target passed to ") + operation
        ));
    }
    if (!value || value.type_id() != fei::type_id<Value>()) {
        return failure(container_error(
            ContainerError::Kind::InvalidElement,
            container_type,
            std::string("Invalid assignment value passed to ") + operation
        ));
    }
    if (target.const_ptr() == value.const_ptr()) {
        return {};
    }

    if constexpr (std::is_copy_assignable_v<Value>) {
        target.get<Value>() = value.get_const<Value>();
        return {};
    } else if constexpr (std::is_move_assignable_v<Value>) {
        if (value.is_const()) {
            return failure(container_error(
                ContainerError::Kind::InvalidElement,
                container_type,
                std::string("Cannot move from const value passed to ") +
                    operation
            ));
        }
        target.get<Value>() = std::move(value.get<Value>());
        return {};
    } else {
        return failure(container_error(
            ContainerError::Kind::UnsupportedOperation,
            container_type,
            std::string("Element type is not assignable for ") + operation
        ));
    }
}

template<class Container>
using ContainerDependencies = std::tuple<>;

} // namespace detail

} // namespace fei
