#pragma once

#include "refl/container_adapter.hpp"
#include "refl/containers/detail.hpp"

#include <cstddef>
#include <string>
#include <tuple>
#include <utility>

namespace fei {

// Adapts std::pair and std::tuple to the same fixed-size indexed container
// model used for arrays. The elements can have different types, so callers must
// query element_type(index) before assigning or deserializing each element.
template<class Container>
class TupleLikeContainerAdapter final : public ContainerAdapter {
  public:
    static constexpr std::size_t c_size = std::tuple_size_v<Container>;

    ContainerKind kind() const override { return ContainerKind::Product; }

    TypeId container_type() const override { return fei::type_id<Container>(); }

    TypeId element_type() const override { return {}; }

    Result<TypeId, ContainerError>
    element_type(std::size_t index) const override {
        return element_type_impl(index, std::make_index_sequence<c_size> {});
    }

    Result<std::size_t, ContainerError> size(Ref container) const override {
        auto result = detail::const_container<Container>(
            container,
            container_type(),
            "size"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        return c_size;
    }

    bool fixed_size() const override { return true; }

    Status<ContainerError> for_each(
        Ref container,
        const ContainerElementVisitor& visitor
    ) const override {
        if (container.is_const()) {
            auto result = detail::const_container<Container>(
                container,
                container_type(),
                "for_each"
            );
            if (!result) {
                return failure(std::move(result.error()));
            }
            return for_each_impl(
                **result,
                container,
                visitor,
                std::make_index_sequence<c_size> {}
            );
        }

        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "for_each"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        return for_each_impl(
            **result,
            container,
            visitor,
            std::make_index_sequence<c_size> {}
        );
    }

    Status<ContainerError>
    assign(Ref container, std::size_t index, Ref value) const override {
        if (index >= c_size) {
            return failure(out_of_range_error(index));
        }
        auto result = detail::mutable_container<Container>(
            container,
            container_type(),
            "assign"
        );
        if (!result) {
            return failure(std::move(result.error()));
        }
        return assign_impl(
            **result,
            container,
            index,
            value,
            std::make_index_sequence<c_size> {}
        );
    }

  private:
    ContainerError out_of_range_error(std::size_t index) const {
        return detail::container_error(
            ContainerError::Kind::OutOfRange,
            container_type(),
            "Tuple-like element index " + std::to_string(index) +
                " is out of range"
        );
    }

    template<std::size_t... Indexes>
    Result<TypeId, ContainerError> element_type_impl(
        std::size_t index,
        std::index_sequence<Indexes...>
    ) const {
        if constexpr (sizeof...(Indexes) == 0) {
            (void)index;
            return failure(out_of_range_error(0));
        } else {
            TypeId result;
            bool found =
                ((index == Indexes ?
                      (result = fei::type_id<
                           std::tuple_element_t<Indexes, Container>>(),
                       true) :
                      false) ||
                 ...);
            if (!found) {
                return failure(out_of_range_error(index));
            }
            return result;
        }
    }

    template<class ContainerRef, std::size_t... Indexes>
    Status<ContainerError> for_each_impl(
        ContainerRef& container,
        Ref owner,
        const ContainerElementVisitor& visitor,
        std::index_sequence<Indexes...>
    ) const {
        Status<ContainerError> status;
        bool keep_visiting = true;
        (
            [&] {
                if (!keep_visiting) {
                    return;
                }
                status = visitor(
                    detail::element_ref(owner, std::get<Indexes>(container)),
                    Indexes
                );
                keep_visiting = static_cast<bool>(status);
            }(),
            ...);
        return status;
    }

    template<class ContainerRef, std::size_t... Indexes>
    Status<ContainerError> assign_impl(
        ContainerRef& container,
        Ref owner,
        std::size_t index,
        Ref value,
        std::index_sequence<Indexes...>
    ) const {
        if constexpr (sizeof...(Indexes) == 0) {
            (void)container;
            (void)owner;
            (void)index;
            (void)value;
            return failure(out_of_range_error(0));
        } else {
            Status<ContainerError> status;
            bool found =
                ((index == Indexes ?
                      (status = detail::assign_ref_value<
                           std::tuple_element_t<Indexes, Container>>(
                           detail::element_ref(
                               owner,
                               std::get<Indexes>(container)
                           ),
                           value,
                           container_type(),
                           "assign"
                       ),
                       true) :
                      false) ||
                 ...);
            if (!found) {
                return failure(out_of_range_error(index));
            }
            return status;
        }
    }
};

} // namespace fei
