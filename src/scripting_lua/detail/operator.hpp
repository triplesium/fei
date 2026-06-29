#pragma once
#include "base/types.hpp"
#include "refl/cls.hpp"
#include "refl/method.hpp"

#include <vector>

namespace fei {

enum class LuaOperator : uint8 {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Pow,
    Unm,
    Len,
    Eq,
    Lt,
    Le,
};

inline const char* get_operator_method_name(LuaOperator op) {
    switch (op) {
        case LuaOperator::Add:
            return "operator+";
        case LuaOperator::Sub:
            return "operator-";
        case LuaOperator::Mul:
            return "operator*";
        case LuaOperator::Div:
            return "operator/";
        case LuaOperator::Mod:
            return "operator%";
        case LuaOperator::Pow:
            return "operator^";
        case LuaOperator::Unm:
            return "operator-";
        case LuaOperator::Len:
            return "operator#";
        case LuaOperator::Eq:
            return "operator==";
        case LuaOperator::Lt:
            return "operator<";
        case LuaOperator::Le:
            return "operator<=";
    }
    return nullptr;
}

inline bool has_operator(Cls& cls, LuaOperator op) {
    return cls.has_method(get_operator_method_name(op));
}

inline Method* get_operator(
    Cls& cls,
    LuaOperator op,
    const std::vector<TypeId>& arg_types,
    MethodConstFilter const_filter = MethodConstFilter::Any
) {
    auto result = cls.try_get_method(
        get_operator_method_name(op),
        arg_types,
        const_filter
    );
    return result ? &*result : nullptr;
}

inline const char* get_operator_metamethod(LuaOperator op) {
    switch (op) {
        case LuaOperator::Add:
            return "__add";
        case LuaOperator::Sub:
            return "__sub";
        case LuaOperator::Mul:
            return "__mul";
        case LuaOperator::Div:
            return "__div";
        case LuaOperator::Mod:
            return "__mod";
        case LuaOperator::Pow:
            return "__pow";
        case LuaOperator::Unm:
            return "__unm";
        case LuaOperator::Len:
            return "__len";
        case LuaOperator::Eq:
            return "__eq";
        case LuaOperator::Lt:
            return "__lt";
        case LuaOperator::Le:
            return "__le";
    }
    return nullptr;
}

} // namespace fei
