#pragma once
#include "refl/argument_adapter.hpp"
#include "refl/callable.hpp"
#include "refl/registry.hpp"
#include "refl/return_adapter.hpp"
#include "refl/type.hpp"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <tuple>
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
        using Type = std::tuple_element_t<Index, std::tuple<Params...>>;
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
    ~Method() override = default;

    InvokeResult
    invoke_variadic(const std::vector<Ref>& args) const override = 0;
    virtual bool accepts_variadic(const std::vector<Ref>& args) const = 0;
    virtual std::optional<int>
    match_score(const std::vector<Ref>& args) const = 0;
    virtual bool is_const() const = 0;
    virtual bool is_static() const = 0;
};

enum class MethodConstFilter {
    Any,
    ConstOnly,
    NonConstOnly,
    PreferConst,
    PreferNonConst,
};

using MethodCallback =
    std::function<InvokeResult(Ref instance, std::span<const Ref> args)>;

class CallbackMethod final : public Method {
  public:
    CallbackMethod(
        TypeId owner_type,
        std::string name,
        std::vector<Param> params,
        QualType return_type,
        bool is_const,
        MethodCallback callback
    ) :
        Method(std::move(name), params, return_type), m_owner_type(owner_type),
        m_is_const(is_const), m_callback(std::move(callback)) {}

    InvokeResult invoke_variadic(const std::vector<Ref>& args) const override {
        if (!match_score(args)) {
            return failure(
                InvokeFailure::invalid_call(
                    "Invalid arguments passed to callback method " + name()
                )
            );
        }
        return m_callback(args.front(), std::span<const Ref>(args).subspan(1));
    }

    bool accepts_variadic(const std::vector<Ref>& args) const override {
        return match_score(args).has_value();
    }

    std::optional<int>
    match_score(const std::vector<Ref>& args) const override {
        if (args.size() != params().size() + 1 || args.empty()) {
            return std::nullopt;
        }
        const auto instance = args.front();
        if (!instance || instance.type_id() != m_owner_type ||
            (!m_is_const && instance.is_const())) {
            return std::nullopt;
        }
        return 0;
    }

    bool is_const() const override { return m_is_const; }
    bool is_static() const override { return false; }

  private:
    TypeId m_owner_type;
    bool m_is_const;
    MethodCallback m_callback;
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
    constexpr static auto c_is_const = SignatureTrait<MethodType>::is_const;

    template<size_t Index>
    using TypeOfParam =
        typename SignatureTrait<MethodType>::template TypeOfParam<Index>::Type;

  public:
    MethodImpl(std::string name, P ptr) :
        Method(std::move(name), make_params(), QualType::of<ReturnType>()),
        m_ptr(ptr) {
        register_reflected_types(std::make_index_sequence<c_params_count>());
    }

    bool is_const() const override { return c_is_const; }

    bool is_static() const override { return c_is_static; }

    bool accepts_variadic(const std::vector<Ref>& args) const override {
        return match_score(args).has_value();
    }

    std::optional<int>
    match_score(const std::vector<Ref>& args) const override {
        if constexpr (c_is_static) {
            if (args.size() != c_params_count) {
                return std::nullopt;
            }
            return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
                return match_params<0, ArgIdx...>(args);
            }(std::make_index_sequence<c_params_count>());
        } else {
            if (args.empty() || args.size() - 1 != c_params_count) {
                return std::nullopt;
            }
            if (!validate_instance(args[0])) {
                return std::nullopt;
            }
            return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
                return match_params<1, ArgIdx...>(args);
            }(std::make_index_sequence<c_params_count>());
        }
    }

    static int score_of(ConversionRank rank) {
        switch (rank) {
            case ConversionRank::Exact:
                return 0;
            case ConversionRank::Weak:
                return 1;
            case ConversionRank::None:
                return 0;
        }
        return 0;
    }

    template<std::size_t Offset, std::size_t... ArgIdx>
    std::optional<int> match_params(const std::vector<Ref>& args) const {
        int score = 0;
        bool matched = true;
        (
            [&] {
                if (!matched) {
                    return;
                }
                auto rank = ArgumentAdapter<TypeOfParam<ArgIdx>>::match(
                    args[ArgIdx + Offset]
                );
                if (rank == ConversionRank::None) {
                    matched = false;
                    return;
                }
                score += score_of(rank);
            }(),
            ...);
        if (!matched) {
            return std::nullopt;
        }
        return score;
    }

    InvokeResult invoke_variadic(const std::vector<Ref>& args) const override {
        if (auto invalid_call_message = validate_call(args)) {
            return invalid_call(std::move(*invalid_call_message));
        }
        return invoke_from_refs(
            args,
            std::make_index_sequence<c_params_count>()
        );
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

    template<class T>
    static void register_type_dependency() {
        using NoRef = std::remove_reference_t<T>;
        using NoPtr = std::remove_pointer_t<NoRef>;
        using Base = std::remove_cv_t<NoPtr>;
        Registry::instance().register_type<Base>();
    }

    template<std::size_t... ArgIdx>
    static void register_reflected_types(std::index_sequence<ArgIdx...>) {
        register_type_dependency<ReturnType>();
        (register_type_dependency<TypeOfParam<ArgIdx>>(), ...);
    }

    InvokeResult invalid_call(std::string message) const {
        return failure(InvokeFailure::invalid_call(std::move(message)));
    }

    std::optional<std::string>
    validate_call(const std::vector<Ref>& args) const {
        if constexpr (c_is_static) {
            if (args.size() != c_params_count) {
                return "Invalid argument count for static method " + name() +
                       ": expected " + std::to_string(c_params_count) +
                       ", got " + std::to_string(args.size());
            }
            if (auto invalid_param =
                    find_invalid_param<0>(args, "static method");
                !invalid_param.empty()) {
                return invalid_param;
            }
        } else {
            if (args.empty() || args.size() - 1 != c_params_count) {
                return "Invalid argument count for method " + name() +
                       ": expected instance plus " +
                       std::to_string(c_params_count) + ", got " +
                       std::to_string(args.size());
            }
            if (!validate_instance(args[0])) {
                return "Invalid instance passed to method " + name();
            }
            if (auto invalid_param = find_invalid_param<1>(args, "method");
                !invalid_param.empty()) {
                return invalid_param;
            }
        }
        return std::nullopt;
    }

    bool validate_instance(const Ref& instance) const {
        if constexpr (c_is_static) {
            return true;
        } else {
            if (!instance ||
                instance.type_id() !=
                    type_id<typename MemberTrait<P>::ParentType>()) {
                return false;
            }
            return c_is_const || !instance.is_const();
        }
    }

    template<std::size_t Offset, std::size_t ArgIdx>
    std::string describe_invalid_param(
        const std::vector<Ref>& args,
        const char* kind
    ) const {
        return "Invalid argument " + std::to_string(ArgIdx + 1) + " for " +
               kind + " " + name() + ": " +
               ArgumentAdapter<TypeOfParam<ArgIdx>>::describe_mismatch(
                   args[ArgIdx + Offset]
               );
    }

    template<std::size_t Offset>
    std::string
    find_invalid_param(const std::vector<Ref>& args, const char* kind) const {
        return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
            return find_invalid_param_impl<Offset, ArgIdx...>(args, kind);
        }(std::make_index_sequence<c_params_count>());
    }

    template<std::size_t Offset, std::size_t... ArgIdx>
    std::string find_invalid_param_impl(
        const std::vector<Ref>& args,
        const char* kind
    ) const {
        std::string message;
        (
            [&] {
                if (!message.empty()) {
                    return;
                }
                if (!ArgumentAdapter<TypeOfParam<ArgIdx>>::accepts(
                        args[ArgIdx + Offset]
                    )) {
                    message =
                        describe_invalid_param<Offset, ArgIdx>(args, kind);
                }
            }(),
            ...);
        return message;
    }

    template<std::size_t... ArgIdx>
    InvokeResult invoke_from_refs(
        const std::vector<Ref>& args,
        std::index_sequence<ArgIdx...>
    ) const {
        if constexpr (c_is_static) {
            return invoke_template(Ref(), args[ArgIdx]...);
        } else {
            return invoke_template(args[0], args[ArgIdx + 1]...);
        }
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
                ArgumentAdapter<TypeOfParam<N>>::get(
                    std::forward<Args>(args)
                )...
            );
        } else if constexpr (c_is_const) {
            return std::invoke(
                m_ptr,
                instance.get_const<typename MemberTrait<P>::ParentType>(),
                ArgumentAdapter<TypeOfParam<N>>::get(
                    std::forward<Args>(args)
                )...
            );
        } else {
            return std::invoke(
                m_ptr,
                instance.get<typename MemberTrait<P>::ParentType>(),
                ArgumentAdapter<TypeOfParam<N>>::get(
                    std::forward<Args>(args)
                )...
            );
        }
    }

    template<class... Args>
    InvokeResult invoke_template(Ref instance, Args&&... args) const {
        if constexpr (c_params_count != sizeof...(Args)) {
            return invalid_call(
                "Internal reflection argument expansion mismatch"
            );
        } else {
            if constexpr (std::same_as<ReturnType, void>) {
                invoke_template_expand(
                    std::make_index_sequence<c_params_count>(),
                    instance,
                    std::forward<Args>(args)...
                );
                return ReturnAdapter<void>::adapt();
            } else {
                decltype(auto) ret = invoke_template_expand(
                    std::make_index_sequence<c_params_count>(),
                    instance,
                    std::forward<Args>(args)...
                );
                return ReturnAdapter<ReturnType>::adapt(
                    std::forward<decltype(ret)>(ret)
                );
            }
        }
    }
};

} // namespace fei
