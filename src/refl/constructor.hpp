#pragma once

#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/qual_type.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <utility>

namespace fei {

class Constructor : public Callable {
  public:
    Constructor(const std::vector<Param>& params, TypeId return_type) :
        Callable("Constructor", params, QualType {return_type}) {}

    ReturnValue
    invoke_variadic(const std::vector<Ref>& args) const override = 0;
    virtual std::vector<TypeId> arg_types() const = 0;
};

template<typename T, typename... Args>
class ConstructorImpl : public Constructor {
  public:
    ConstructorImpl() :
        Constructor({Param {"args", QualType::of<Args>()}...}, type_id<T>()) {}

    ReturnValue invoke_variadic(const std::vector<Ref>& args) const override {
        if (!validate_args(args)) {
            error("Invalid arguments passed to constructor");
            return {};
        }
        return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
            return make_val<T>(args[ArgIdx].as<Args>()...);
        }(std::make_index_sequence<sizeof...(Args)>());
    }

    std::vector<TypeId> arg_types() const override {
        return {QualType::of<Args>().type_id()...};
    }

  private:
    template<std::size_t... ArgIdx>
    bool validate_args_impl(
        const std::vector<Ref>& args,
        std::index_sequence<ArgIdx...>
    ) const {
        return (args[ArgIdx].can_as<Args>() && ...);
    }

    bool validate_args(const std::vector<Ref>& args) const {
        if (args.size() != sizeof...(Args)) {
            return false;
        }
        return validate_args_impl(
            args,
            std::make_index_sequence<sizeof...(Args)>()
        );
    }
};

} // namespace fei
