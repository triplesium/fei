#pragma once

#include "base/result.hpp"
#include "refl/callable.hpp"
#include "refl/ref_utils.hpp"
#include "refl/val.hpp"

#include <type_traits>
#include <utility>

namespace fei {

template<class T>
struct ReturnAdapter {
    template<class U>
    static InvokeResult adapt(U&& value) {
        using ValueType = std::remove_cvref_t<U>;
        return ReturnValue::value(make_val<ValueType>(std::forward<U>(value)));
    }
};

template<>
struct ReturnAdapter<void> {
    static InvokeResult adapt() { return ReturnValue::void_value(); }
};

template<class T>
struct ReturnAdapter<T&> {
    static InvokeResult adapt(T& value) {
        return ReturnValue::reference(make_ref(value));
    }
};

template<class T>
struct ReturnAdapter<T*> {
    static InvokeResult adapt(T* value) {
        return ReturnValue::reference(make_ref(value));
    }
};

template<>
struct ReturnAdapter<Val> {
    static InvokeResult adapt(Val value) {
        return ReturnValue::value(std::move(value));
    }
};

template<>
struct ReturnAdapter<Ref> {
    static InvokeResult adapt(Ref ref) { return ReturnValue::reference(ref); }
};

template<class T, class E>
struct ReturnAdapter<Result<T, E>> {
    static InvokeResult adapt(Result<T, E>&& result) {
        if (!result) {
            return failure(
                InvokeFailure::returned_error(
                    make_val<E>(std::move(result.error()))
                )
            );
        }

        if constexpr (std::same_as<T, void>) {
            return ReturnValue::status();
        } else {
            return ReturnAdapter<T>::adapt(std::move(*result));
        }
    }
};

} // namespace fei
