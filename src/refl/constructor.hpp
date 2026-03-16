#pragma once

#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/qual_type.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

namespace fei {

class Constructor : public Callable {
  public:
    Constructor(const std::vector<Param>& params, TypeId return_type) :
        Callable("Constructor", params, QualType {return_type}) {}

    virtual ReturnValue invoke_variadic(const std::vector<Ref>& args) const = 0;
    virtual std::vector<TypeId> arg_types() const = 0;
};

template<typename T, typename... Args>
class ConstructorImpl : public Constructor {
  public:
    ConstructorImpl() :
        Constructor({Param {"args", type_id<Args>()}...}, type_id<T>()) {}

    virtual ReturnValue invoke_variadic(const std::vector<Ref>& args
    ) const override {
        if (!validate(args)) {
            error("Invalid arguments passed to constructor");
            return {};
        }
        return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
            return make_val<T>(ref_to_arg<Args>(args[ArgIdx])...);
        }(std::make_index_sequence<sizeof...(Args)>());
    }

    virtual std::vector<TypeId> arg_types() const override {
        return {type_id<Args>()...};
    }

  private:
    template<typename T>
    decltype(auto) ref_to_arg(Ref ref) const {
        if constexpr (std::is_rvalue_reference_v<T>) {
            return ref.get_rref<T>();
        } else {
            return ref.get<T>();
        }
    }
};

} // namespace fei
