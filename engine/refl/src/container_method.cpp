#include "container_method.hpp"

#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/registry.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fei {
namespace {

InvokeResult invalid_call(std::string message) {
    return failure(InvokeFailure::invalid_call(std::move(message)));
}

InvokeResult returned_error(ContainerError error) {
    return failure(
        InvokeFailure::returned_error(
            make_val<ContainerError>(std::move(error))
        )
    );
}

InvokeResult adapt_result(Result<std::size_t, ContainerError> result) {
    if (!result) {
        return returned_error(std::move(result.error()));
    }
    return ReturnValue::value(make_val<std::size_t>(*result));
}

InvokeResult adapt_result(Result<bool, ContainerError> result) {
    if (!result) {
        return returned_error(std::move(result.error()));
    }
    return ReturnValue::value(make_val<bool>(*result));
}

InvokeResult adapt_result(Result<Ref, ContainerError> result) {
    if (!result) {
        return returned_error(std::move(result.error()));
    }
    return ReturnValue::reference(*result);
}

InvokeResult adapt_result(Status<ContainerError> result) {
    if (!result) {
        return returned_error(std::move(result.error()));
    }
    return ReturnValue::status();
}

Result<ContainerAdapter&, InvokeFailure>
resolve_adapter(Ref instance, const char* operation) {
    auto adapter =
        Registry::instance().try_get_container_adapter(instance.type_id());
    if (!adapter) {
        return failure(
            InvokeFailure::invalid_call(
                std::string("Cannot call container ") + operation + ": " +
                adapter.error().message
            )
        );
    }
    return *adapter;
}

Result<IndexedContainerAdapter&, InvokeFailure>
resolve_indexed(Ref instance, const char* operation) {
    auto adapter = resolve_adapter(instance, operation);
    if (!adapter) {
        return failure(std::move(adapter.error()));
    }
    auto* indexed = adapter->indexed();
    if (!indexed) {
        return failure(
            InvokeFailure::invalid_call(
                std::string("Container does not support indexed ") + operation
            )
        );
    }
    return *indexed;
}

Result<AssociativeContainerAdapter&, InvokeFailure>
resolve_associative(Ref instance, const char* operation) {
    auto adapter = resolve_adapter(instance, operation);
    if (!adapter) {
        return failure(std::move(adapter.error()));
    }
    auto* associative = adapter->associative();
    if (!associative) {
        return failure(
            InvokeFailure::invalid_call(
                std::string("Container does not support associative ") +
                operation
            )
        );
    }
    return *associative;
}

Result<std::size_t, InvokeFailure>
read_index(Ref value, const char* operation) {
    if (!value) {
        return failure(
            InvokeFailure::invalid_call(
                std::string("Container ") + operation + " index cannot be empty"
            )
        );
    }
    if (value.type_id() == type_id<std::size_t>()) {
        return value.get_const<std::size_t>();
    }
    if (value.type_id() == type_id<unsigned int>()) {
        return static_cast<std::size_t>(value.get_const<unsigned int>());
    }
    if (value.type_id() == type_id<int>()) {
        const auto index = value.get_const<int>();
        if (index >= 0) {
            return static_cast<std::size_t>(index);
        }
    }
    return failure(
        InvokeFailure::invalid_call(
            std::string("Container ") + operation +
            " index must be a non-negative integer"
        )
    );
}

InvokeResult invoke_size(Ref instance, std::span<const Ref>) {
    auto adapter = resolve_adapter(instance, "size");
    if (!adapter) {
        return failure(std::move(adapter.error()));
    }
    return adapt_result(adapter->size(instance));
}

InvokeResult invoke_at(Ref instance, std::span<const Ref> args) {
    auto indexed = resolve_indexed(instance, "at");
    if (!indexed) {
        return failure(std::move(indexed.error()));
    }
    auto index = read_index(args[0], "at");
    if (!index) {
        return failure(std::move(index.error()));
    }
    return adapt_result(indexed->at(instance, *index));
}

InvokeResult invoke_assign(Ref instance, std::span<const Ref> args) {
    auto indexed = resolve_indexed(instance, "assign");
    if (!indexed) {
        return failure(std::move(indexed.error()));
    }
    auto index = read_index(args[0], "assign");
    if (!index) {
        return failure(std::move(index.error()));
    }
    return adapt_result(indexed->assign(instance, *index, args[1]));
}

InvokeResult invoke_append(Ref instance, std::span<const Ref> args) {
    auto indexed = resolve_indexed(instance, "append");
    if (!indexed) {
        return failure(std::move(indexed.error()));
    }
    return adapt_result(indexed->append(instance, args[0]));
}

InvokeResult invoke_indexed_insert(Ref instance, std::span<const Ref> args) {
    auto indexed = resolve_indexed(instance, "insert");
    if (!indexed) {
        return failure(std::move(indexed.error()));
    }
    auto index = read_index(args[0], "insert");
    if (!index) {
        return failure(std::move(index.error()));
    }
    return adapt_result(indexed->insert(instance, *index, args[1]));
}

InvokeResult invoke_indexed_erase(Ref instance, std::span<const Ref> args) {
    auto indexed = resolve_indexed(instance, "erase");
    if (!indexed) {
        return failure(std::move(indexed.error()));
    }
    auto index = read_index(args[0], "erase");
    if (!index) {
        return failure(std::move(index.error()));
    }
    return adapt_result(indexed->erase(instance, *index));
}

InvokeResult invoke_clear(Ref instance, std::span<const Ref>) {
    auto adapter = resolve_adapter(instance, "clear");
    if (!adapter) {
        return failure(std::move(adapter.error()));
    }
    if (auto* indexed = adapter->indexed()) {
        return adapt_result(indexed->clear(instance));
    }
    if (auto* associative = adapter->associative()) {
        return adapt_result(associative->clear(instance));
    }
    return invalid_call("Container does not support clear");
}

InvokeResult invoke_find(Ref instance, std::span<const Ref> args) {
    auto associative = resolve_associative(instance, "find");
    if (!associative) {
        return failure(std::move(associative.error()));
    }
    return adapt_result(associative->find(instance, args[0]));
}

InvokeResult invoke_contains(Ref instance, std::span<const Ref> args) {
    auto associative = resolve_associative(instance, "contains");
    if (!associative) {
        return failure(std::move(associative.error()));
    }
    return adapt_result(associative->contains(instance, args[0]));
}

InvokeResult
invoke_associative_insert(Ref instance, std::span<const Ref> args) {
    auto associative = resolve_associative(instance, "insert");
    if (!associative) {
        return failure(std::move(associative.error()));
    }
    return adapt_result(associative->insert(
        instance,
        AssociativeElementRef {
            .key = args[0],
            .value = associative->has_mapped_value() ? args[1] : Ref {},
        }
    ));
}

InvokeResult invoke_associative_erase(Ref instance, std::span<const Ref> args) {
    auto associative = resolve_associative(instance, "erase");
    if (!associative) {
        return failure(std::move(associative.error()));
    }
    return adapt_result(associative->erase(instance, args[0]));
}

std::vector<Param> params(std::initializer_list<const char*> names) {
    std::vector<Param> result;
    result.reserve(names.size());
    for (const auto* name : names) {
        result.push_back(Param::dynamic(name));
    }
    return result;
}

void add_method(
    Cls& cls,
    TypeId owner_type,
    std::string name,
    std::vector<Param> method_params,
    QualType return_type,
    bool is_const,
    MethodCallback callback
) {
    cls.add_method(
        std::make_unique<CallbackMethod>(
            owner_type,
            std::move(name),
            std::move(method_params),
            return_type,
            is_const,
            std::move(callback)
        )
    );
}

} // namespace

void register_container_methods(Cls& cls, const ContainerAdapter& adapter) {
    const auto owner_type = adapter.container_type();
    add_method(
        cls,
        owner_type,
        "size",
        {},
        QualType::of<std::size_t>(),
        true,
        &invoke_size
    );

    if (const auto* indexed = adapter.indexed()) {
        add_method(
            cls,
            owner_type,
            "at",
            params({"index"}),
            QualType {},
            true,
            &invoke_at
        );
        add_method(
            cls,
            owner_type,
            "assign",
            params({"index", "value"}),
            QualType::of<void>(),
            false,
            &invoke_assign
        );

        const bool structurally_mutable =
            adapter.kind() == ContainerKind::Optional ||
            (adapter.kind() == ContainerKind::Sequence &&
             !indexed->fixed_size());
        if (!structurally_mutable) {
            return;
        }

        add_method(
            cls,
            owner_type,
            "append",
            params({"value"}),
            QualType::of<void>(),
            false,
            &invoke_append
        );
        add_method(
            cls,
            owner_type,
            "insert",
            params({"index", "value"}),
            QualType::of<void>(),
            false,
            &invoke_indexed_insert
        );
        add_method(
            cls,
            owner_type,
            "erase",
            params({"index"}),
            QualType::of<void>(),
            false,
            &invoke_indexed_erase
        );
        add_method(
            cls,
            owner_type,
            "clear",
            {},
            QualType::of<void>(),
            false,
            &invoke_clear
        );
        return;
    }

    const auto* associative = adapter.associative();
    if (!associative) {
        return;
    }
    add_method(
        cls,
        owner_type,
        "find",
        params({"key"}),
        QualType {},
        true,
        &invoke_find
    );
    add_method(
        cls,
        owner_type,
        "contains",
        params({"key"}),
        QualType::of<bool>(),
        true,
        &invoke_contains
    );
    add_method(
        cls,
        owner_type,
        "insert",
        associative->has_mapped_value() ? params({"key", "value"}) :
                                          params({"key"}),
        QualType::of<void>(),
        false,
        &invoke_associative_insert
    );
    add_method(
        cls,
        owner_type,
        "erase",
        params({"key"}),
        QualType::of<void>(),
        false,
        &invoke_associative_erase
    );
    add_method(
        cls,
        owner_type,
        "clear",
        {},
        QualType::of<void>(),
        false,
        &invoke_clear
    );
}

} // namespace fei
