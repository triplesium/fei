#include "refl/reflected_conversion.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <utility>

namespace ets {
namespace {

thread_local unsigned int converting_constructor_depth = 0;

class ConvertingConstructorGuard {
  public:
    ConvertingConstructorGuard() { ++converting_constructor_depth; }
    ~ConvertingConstructorGuard() { --converting_constructor_depth; }

    ConvertingConstructorGuard(const ConvertingConstructorGuard&) = delete;
    ConvertingConstructorGuard&
    operator=(const ConvertingConstructorGuard&) = delete;
};

} // namespace

ConversionRank
reflected_conversion_rank(TypeId target_type, const Ref& source) {
    if (!source || source.type_id() == target_type) {
        return ConversionRank::None;
    }
    if (converting_constructor_depth != 0) {
        return ConversionRank::None;
    }

    ConvertingConstructorGuard guard;
    auto cls = Registry::instance().try_get_cls(target_type);
    if (!cls) {
        return ConversionRank::None;
    }
    return cls->get_converting_constructor_for_arg(source) ?
               ConversionRank::Weak :
               ConversionRank::None;
}

Result<Val, InvokeFailure>
reflected_convert(TypeId target_type, const Ref& source) {
    if (!source) {
        return failure(
            InvokeFailure::invalid_call("Cannot convert an empty Ref")
        );
    }
    if (source.type_id() == target_type) {
        auto copied = Val::copy(source);
        if (!copied) {
            return failure(
                InvokeFailure::invalid_call(std::move(copied.error().message))
            );
        }
        return std::move(*copied);
    }
    if (converting_constructor_depth != 0) {
        return failure(
            InvokeFailure::invalid_call(
                "A reflected conversion cannot contain another reflected "
                "conversion"
            )
        );
    }

    ConvertingConstructorGuard guard;
    auto cls = Registry::instance().try_get_cls(target_type);
    if (!cls) {
        return failure(InvokeFailure::invalid_call(cls.error().message));
    }
    auto constructor = cls->get_converting_constructor_for_arg(source);
    if (!constructor) {
        return failure(std::move(constructor.error()));
    }
    auto invoked = constructor->invoke(source);
    if (!invoked) {
        return failure(std::move(invoked.error()));
    }
    if (!invoked->is_value()) {
        return failure(
            InvokeFailure::invalid_call(
                "Converting constructor did not return an owned value"
            )
        );
    }
    return std::move(invoked->value());
}

} // namespace ets
