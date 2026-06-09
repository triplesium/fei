#pragma once
#include "base/log.hpp"
#include "refl/callable.hpp"
#include "refl/ref_utils.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <array>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

template<class>
struct SignatureTraitBase;

template<class Ret, class... Params>
struct SignatureTraitBase<Ret(Params...)> {
    using ReturnType = Ret;
    static constexpr auto params_count = sizeof...(Params);

    static inline std::array<QualType, params_count> param_types() {
        return {QualType::of<Params>()...};
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
struct SignatureTrait<Ret(Params...)> : SignatureTraitBase<Ret(Params...)> {
    static constexpr bool is_const = false;
};

template<class Ret, class... Params>
struct SignatureTrait<Ret(Params...) const>
    : SignatureTraitBase<Ret(Params...)> {
    static constexpr bool is_const = true;
};

class Method : public Callable {
  public:
    Method(
        std::string name,
        const std::vector<Param>& params,
        const QualType& return_type
    ) : Callable(name, params, return_type) {}
    virtual ~Method() = default;

    virtual ReturnValue invoke_variadic(const std::vector<Ref>& args) const = 0;
    virtual bool is_const_method() const = 0;
    virtual bool is_static_method() const = 0;
};

enum class MethodConstFilter {
    Any,
    ConstOnly,
    NonConstOnly,
    PreferConst,
    PreferNonConst,
};

template<typename P>
class MethodImpl : public Method {
  private:
    P m_ptr;
    using MethodType = typename MemberTrait<P>::Type;
    using ReturnType = typename SignatureTrait<MethodType>::ReturnType;
    constexpr static auto c_params_count =
        SignatureTrait<MethodType>::params_count;
    constexpr static auto c_is_static = MemberTrait<P>::is_static;
    constexpr static auto c_is_const_method = SignatureTrait<MethodType>::is_const;

    template<size_t Index>
    using TypeOfParam =
        typename SignatureTrait<MethodType>::template TypeOfParam<Index>::Type;

  public:
    MethodImpl(std::string name, P ptr) :
        Method(std::move(name), make_params(), QualType::of<ReturnType>()),
        m_ptr(ptr) {}

    bool is_const_method() const override { return c_is_const_method; }

    bool is_static_method() const override { return c_is_static; }

    ReturnValue invoke_variadic(const std::vector<Ref>& args) const override {
        if (c_is_static) {
            if (args.size() != c_params_count) {
                error(
                    "Invalid argument count for static method {}: expected {}, "
                    "got {}",
                    name(),
                    c_params_count,
                    args.size()
                );
                return {};
            }
            return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
                if (!validate_params<0, ArgIdx...>(args)) {
                    error("Invalid argument type passed to static method {}", name());
                    return ReturnValue {};
                }
                // static method does not need an instance
                return invoke_template(Ref(), args[ArgIdx]...);
            }(std::make_index_sequence<c_params_count>());
        } else {
            if (args.size() == 0 || args.size() - 1 != c_params_count) {
                error(
                    "Invalid argument count for method {}: expected {}, got "
                    "{}",
                    name(),
                    c_params_count,
                    args.size() == 0 ? 0 : args.size() - 1
                );
                return {};
            }
            if (!validate_instance(args[0])) {
                error("Invalid instance passed to method {}", name());
                return {};
            }
            return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
                if (!validate_params<1, ArgIdx...>(args)) {
                    error("Invalid argument type passed to method {}", name());
                    return ReturnValue {};
                }
                // arg[0] is the object instance itself
                return invoke_template(args[0], args[ArgIdx + 1]...);
            }(std::make_index_sequence<c_params_count>());
        }
    }

  private:
    static std::vector<Param> make_params() {
        auto types = SignatureTrait<MethodType>::param_types();
        std::vector<Param> params;
        params.reserve(types.size());
        for (auto type : types) {
            params.emplace_back("", type);
        }
        return params;
    }

    bool validate_instance(const Ref& instance) const {
        if constexpr (c_is_static) {
            return true;
        } else {
            if (!instance ||
                instance.type_id() != type_id<typename MemberTrait<P>::ParentType>()) {
                return false;
            }
            return c_is_const_method || !instance.is_const();
        }
    }

    template<std::size_t Offset, std::size_t... ArgIdx>
    bool validate_params(const std::vector<Ref>& args) const {
        return (args[ArgIdx + Offset].can_as<TypeOfParam<ArgIdx>>() && ...);
    }

    template<class... Args, size_t... N>
    decltype(auto) invoke_template_expand(
        std::index_sequence<N...>,
        Ref instance,
        Args&&... args
    ) const {
        if constexpr (c_is_static) {
            return std::invoke(
                m_ptr,
                std::forward<Args>(args).template as<TypeOfParam<N>>()...
            );
        } else if constexpr (c_is_const_method) {
            return std::invoke(
                m_ptr,
                instance.get_const<typename MemberTrait<P>::ParentType>(),
                std::forward<Args>(args).template as<TypeOfParam<N>>()...
            );
        } else {
            return std::invoke(
                m_ptr,
                instance.get<typename MemberTrait<P>::ParentType>(),
                std::forward<Args>(args).template as<TypeOfParam<N>>()...
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
                    return make_val<ReturnType>(std::move(ret));
                }
            }
        }
    }
};

} // namespace fei
