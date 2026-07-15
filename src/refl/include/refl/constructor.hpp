#pragma once

#include "refl/argument_adapter.hpp"
#include "refl/callable.hpp"
#include "refl/qual_type.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace fei {

class Constructor : public Callable {
  public:
    Constructor(const std::vector<Param>& params, TypeId return_type) :
        Callable("Constructor", params, QualType {return_type}) {}

    InvokeResult
    invoke_variadic(const std::vector<Ref>& args) const override = 0;
    virtual bool accepts_variadic(const std::vector<Ref>& args) const = 0;
    virtual std::optional<int>
    match_score(const std::vector<Ref>& args) const = 0;
    virtual std::vector<TypeId> arg_types() const = 0;
};

template<typename T, typename... Args>
class ConstructorImpl : public Constructor {
  public:
    ConstructorImpl() :
        Constructor({Param {"args", QualType::of<Args>()}...}, type_id<T>()) {
        register_type_dependency<T>();
        (register_type_dependency<Args>(), ...);
    }

    bool accepts_variadic(const std::vector<Ref>& args) const override {
        return match_score(args).has_value();
    }

    std::optional<int>
    match_score(const std::vector<Ref>& args) const override {
        if (args.size() != sizeof...(Args)) {
            return std::nullopt;
        }
        return match_args(args, std::make_index_sequence<sizeof...(Args)>());
    }

    InvokeResult invoke_variadic(const std::vector<Ref>& args) const override {
        if (args.size() != sizeof...(Args)) {
            return invalid_call(
                "Invalid argument count for constructor: expected " +
                std::to_string(sizeof...(Args)) + ", got " +
                std::to_string(args.size())
            );
        }
        auto invalid_arg =
            find_invalid_arg(args, std::make_index_sequence<sizeof...(Args)>());
        if (!invalid_arg.empty()) {
            return invalid_call(std::move(invalid_arg));
        }
        return [&]<size_t... ArgIdx>(std::index_sequence<ArgIdx...>) {
            return ReturnValue::value(
                make_val<T>(ArgumentAdapter<Args>::get(args[ArgIdx])...)
            );
        }(std::make_index_sequence<sizeof...(Args)>());
    }

    std::vector<TypeId> arg_types() const override {
        return {QualType::of<Args>().type_id()...};
    }

  private:
    template<class U>
    static void register_type_dependency() {
        using NoRef = std::remove_reference_t<U>;
        using NoPtr = std::remove_pointer_t<NoRef>;
        using Base = std::remove_cv_t<NoPtr>;
        Registry::instance().register_type<Base>();
    }

    InvokeResult invalid_call(std::string message) const {
        return failure(InvokeFailure::invalid_call(std::move(message)));
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

    template<std::size_t... ArgIdx>
    std::optional<int> match_args(
        const std::vector<Ref>& args,
        std::index_sequence<ArgIdx...>
    ) const {
        int score = 0;
        bool matched = true;
        (
            [&] {
                if (!matched) {
                    return;
                }
                auto rank = ArgumentAdapter<Args>::match(args[ArgIdx]);
                if (rank == ConversionRank::None) {
                    matched = false;
                    return;
                }
                score += score_of(rank);
            }(),
            ...
        );
        if (!matched) {
            return std::nullopt;
        }
        return score;
    }

    template<std::size_t... ArgIdx>
    std::string find_invalid_arg(
        const std::vector<Ref>& args,
        std::index_sequence<ArgIdx...>
    ) const {
        std::string message;
        (
            [&] {
                if (!message.empty()) {
                    return;
                }
                if (!ArgumentAdapter<Args>::accepts(args[ArgIdx])) {
                    message =
                        "Invalid argument " + std::to_string(ArgIdx + 1) +
                        " for constructor: " +
                        ArgumentAdapter<Args>::describe_mismatch(args[ArgIdx]);
                }
            }(),
            ...
        );
        return message;
    }
};

} // namespace fei
