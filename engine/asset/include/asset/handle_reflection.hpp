#pragma once

#include "asset/handle.hpp"
#include "refl/argument_adapter.hpp"

namespace ets {

template<>
struct ArgumentAdapter<const UntypedHandle&> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == type_id<UntypedHandle>()) {
            return ConversionRank::Exact;
        }
        return convert_to_untyped_handle(ref) ? ConversionRank::Weak :
                                                ConversionRank::None;
    }

    static bool accepts(const Ref& ref) {
        return match(ref) != ConversionRank::None;
    }

    static UntypedHandle get(const Ref& ref) {
        if (ref.type_id() == type_id<UntypedHandle>()) {
            return ref.get_const<UntypedHandle>();
        }
        auto converted = convert_to_untyped_handle(ref);
        ETS_ASSERT(converted);
        return std::move(*converted);
    }

    static std::string expected_type() {
        return std::string(type_name<UntypedHandle>());
    }

    static std::string actual_type(const Ref& ref) {
        return detail::describe_ref(ref);
    }

    static std::string describe_mismatch(const Ref& ref) {
        return "expected " + expected_type() + ", got " + actual_type(ref);
    }
};

} // namespace ets
