#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace fei {

struct ContainerError {
    enum class Kind {
        InvalidContainer,
        InvalidElement,
        UnsupportedOperation,
        OutOfRange,
    };

    Kind kind;
    TypeId container_type;
    std::string message;

    static ContainerError
    make(Kind kind, TypeId container_type, std::string message) {
        return ContainerError {
            .kind = kind,
            .container_type = container_type,
            .message = std::move(message),
        };
    }
};

struct AssociativeElementRef {
    Ref key;
    Ref value;
};

enum class ContainerKind {
    // Homogeneous indexed containers, including fixed-size arrays.
    Sequence,
    // A nullable container with zero or one homogeneous value.
    Optional,
    // Fixed-size heterogeneous containers such as pair and tuple.
    Product,
    // Associative key-value containers.
    Map,
    // Associative key-only containers.
    Set,
};

using ContainerElementVisitor =
    std::function<Status<ContainerError>(Ref element, std::size_t index)>;

using ContainerEntryVisitor = std::function<
    Status<ContainerError>(AssociativeElementRef entry, std::size_t index)>;

class AssociativeContainerAdapter;

// Stateless, zero-copy adapter for one reflected C++ container type. Returned
// element Refs are borrowed from the supplied container instance.
class ContainerAdapter {
  public:
    virtual ~ContainerAdapter() = default;

    virtual ContainerKind kind() const = 0;
    virtual TypeId container_type() const = 0;
    // Homogeneous containers report their common element type here.
    // Heterogeneous containers may return an empty TypeId; callers should use
    // element_type(index) while iterating them.
    virtual TypeId element_type() const = 0;
    virtual Result<TypeId, ContainerError>
    element_type(std::size_t index) const {
        (void)index;
        return element_type();
    }

    virtual Result<std::size_t, ContainerError> size(Ref container) const = 0;
    virtual bool fixed_size() const = 0;

    virtual const AssociativeContainerAdapter* associative() const {
        return nullptr;
    }
    virtual AssociativeContainerAdapter* associative() { return nullptr; }

    // Visitors must not structurally modify the container while enumeration
    // is active. Borrowed element Refs must not outlive the container or a
    // structural mutation.
    virtual Status<ContainerError>
    for_each(Ref container, const ContainerElementVisitor& visitor) const = 0;

    virtual Status<ContainerError>
    assign(Ref container, std::size_t index, Ref value) const {
        (void)index;
        (void)value;
        return unsupported(container, "assign");
    }

    virtual Status<ContainerError> clear(Ref container) const {
        return unsupported(container, "clear");
    }

    virtual Status<ContainerError> append(Ref container, Ref value) const {
        (void)value;
        return unsupported(container, "append");
    }

  protected:
    ContainerError unsupported_error(const char* operation) const {
        return ContainerError::make(
            ContainerError::Kind::UnsupportedOperation,
            container_type(),
            std::string("Container adapter does not support ") + operation
        );
    }

    Status<ContainerError>
    unsupported(Ref container, const char* operation) const {
        (void)container;
        return failure(unsupported_error(operation));
    }
};

class AssociativeContainerAdapter : public ContainerAdapter {
  public:
    bool has_mapped_value() const { return kind() == ContainerKind::Map; }

    const AssociativeContainerAdapter* associative() const override {
        return this;
    }

    AssociativeContainerAdapter* associative() override { return this; }

    virtual TypeId key_type() const = 0;
    virtual TypeId mapped_type() const { return {}; }

    virtual Status<ContainerError> for_each_entry(
        Ref container,
        const ContainerEntryVisitor& visitor
    ) const = 0;

    virtual Status<ContainerError>
    insert(Ref container, AssociativeElementRef entry) const = 0;
};

} // namespace fei
