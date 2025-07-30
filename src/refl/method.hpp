#pragma once
#include "refl/callable.hpp"
#include "refl/ref_utils.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <array>

namespace fei {

template<class>
struct SignatureTraitBase;

template<class Ret, class... Params>
struct SignatureTraitBase<Ret(Params...)> {
    using ReturnType = Ret;
    static constexpr auto params_count = sizeof...(Params);

    static inline std::array<TypeId, params_count> param_types() {
        return {type_id<Params>()...};
    }

    template<size_t Index>
    struct TypeOfParam {
        using Type =
            typename std::tuple_element<Index, std::tuple<Params...>>::type;
    };
};

template<class>
struct SignatureTrait;

template<class Ret, class... Params>
struct SignatureTrait<Ret(Params...)> : SignatureTraitBase<Ret(Params...)> {};

template<class Ret, class... Params>
struct SignatureTrait<Ret(Params...) const>
    : SignatureTraitBase<Ret(Params...)> {};

class Method : public Callable {
  public:
    Method(
        std::string_view name,
        const std::vector<Param>& params,
        const QualType& return_type
    ) : Callable(name, params, return_type) {}
    virtual ~Method() = default;

    virtual ReturnValue invoke_variadic(const std::vector<Ref>& args) const = 0;
};

template<typename P>
class TMethod : public Method {
  private:
    P m_ptr;
    using MethodType = typename MemberTrait<P>::Type;
    using ReturnType = typename SignatureTrait<MethodType>::ReturnType;
    constexpr static auto c_params_count =
        SignatureTrait<MethodType>::params_count;
    constexpr static auto c_is_static = MemberTrait<P>::is_static;

    template<size_t Index>
    using TypeOfParam =
        typename SignatureTrait<MethodType>::template TypeOfParam<Index>::Type;

  public:
    TMethod(std::string name, P ptr) :
        Method(name, {}, type_id<ReturnType>()), m_ptr(ptr) {
        auto types = SignatureTrait<MethodType>::param_types();
    }

    ReturnValue invoke_variadic(const std::vector<Ref>& args) const override {
        if (c_is_static) {
            if (args.size() != c_params_count)
                return {};
            return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
                // static method does not need an instance
                return invoke_template(Ref(), args[ArgIdx]...);
            }(std::make_index_sequence<c_params_count>());
        } else {
            if (args.size() - 1 != c_params_count)
                return {};
            return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
                // arg[0] is the object instance itself
                return invoke_template(args[0], args[ArgIdx + 1]...);
            }(std::make_index_sequence<c_params_count>());
        }
    }

  private:
    template<class... Args, size_t... N>
    decltype(auto) invoke_template_expand(
        std::index_sequence<N...>,
        Ref instance,
        Args&&... args
    ) const {
        if constexpr (c_is_static) {
            return std::invoke(
                m_ptr,
                std::forward<Args>(args).template get<TypeOfParam<N>>()...
            );
        } else {
            return std::invoke(
                m_ptr,
                instance.get<typename MemberTrait<P>::ParentType>(),
                std::forward<Args>(args).template get<TypeOfParam<N>>()...
            );
        }
    }

    template<class... Args>
    ReturnValue invoke_template(Ref instance, Args&&... args) const {
        if constexpr (c_params_count != sizeof...(Args)) {
            return ReturnValue {};
        } else {
            if constexpr (std::same_as<ReturnType, void>) {
                invoke_template_expand(
                    std::make_index_sequence<c_params_count>(),
                    instance,
                    std::forward<Args>(args)...
                );
                return ReturnValue {};
            } else {
                decltype(auto) ret = invoke_template_expand(
                    std::make_index_sequence<c_params_count>(),
                    instance,
                    std::forward<Args>(args)...
                );
                if constexpr (std::is_pointer_v<ReturnType> ||
                              std::is_reference_v<ReturnType>) {
                    return make_ref(ret);
                } else {
                    return make_val<ReturnType>(ret);
                }
            }
        }
    }
};

} // namespace fei
