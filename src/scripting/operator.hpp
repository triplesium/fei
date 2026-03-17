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

inline Method* get_operator(Cls& cls, LuaOperator op) {
    switch (op) {
        case LuaOperator::Add:
            return cls.get_method("operator+", {cls.type_id()});
        case LuaOperator::Sub:
            return cls.get_method("operator-", {cls.type_id()});
        case LuaOperator::Mul:
            return cls.get_method("operator*", {cls.type_id()});
        case LuaOperator::Div:
            return cls.get_method("operator/", {cls.type_id()});
        case LuaOperator::Mod:
            return cls.get_method("operator%", {cls.type_id()});
        case LuaOperator::Pow:
            return cls.get_method("operator^", {cls.type_id()});
        case LuaOperator::Unm:
            return cls.get_method("operator-", {});
        case LuaOperator::Len:
            return cls.get_method("operator#", {});
        case LuaOperator::Eq:
            return cls.get_method("operator==", {cls.type_id()});
        case LuaOperator::Lt:
            return cls.get_method("operator<", {cls.type_id()});
        case LuaOperator::Le:
            return cls.get_method("operator<=", {cls.type_id()});
    }
    return nullptr;
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
