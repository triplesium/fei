#pragma once

#include <functional>
#if !defined(__cpp_lib_move_only_function) || \
    __cpp_lib_move_only_function < 202110L
#    include <concepts>
#    include <memory>
#    include <type_traits>
#    include <utility>
#endif

namespace fei {

#if defined(__cpp_lib_move_only_function) && \
    __cpp_lib_move_only_function >= 202110L

template<typename Signature>
using MoveOnlyFunction = std::move_only_function<Signature>;

#else

template<typename Signature>
class MoveOnlyFunction;

template<typename Return, typename... Args>
class MoveOnlyFunction<Return(Args...)> {
  public:
    MoveOnlyFunction() = default;
    MoveOnlyFunction(std::nullptr_t) noexcept {}

    template<typename Function>
        requires(
            !std::same_as<std::remove_cvref_t<Function>, MoveOnlyFunction> &&
            std::is_invocable_r_v<Return, Function&, Args...>
        )
    MoveOnlyFunction(Function&& function) :
        m_function(
            std::make_unique<Model<std::remove_cvref_t<Function>>>(
                std::forward<Function>(function)
            )
        ) {}

    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;

    MoveOnlyFunction& operator=(std::nullptr_t) noexcept {
        m_function.reset();
        return *this;
    }

    template<typename Function>
        requires(
            !std::same_as<std::remove_cvref_t<Function>, MoveOnlyFunction> &&
            std::is_invocable_r_v<Return, Function&, Args...>
        )
    MoveOnlyFunction& operator=(Function&& function) {
        MoveOnlyFunction replacement(std::forward<Function>(function));
        swap(replacement);
        return *this;
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(m_function);
    }

    Return operator()(Args... args) {
        if (!m_function) {
            throw std::bad_function_call();
        }
        return m_function->invoke(std::forward<Args>(args)...);
    }

    void swap(MoveOnlyFunction& other) noexcept {
        m_function.swap(other.m_function);
    }

  private:
    struct Concept {
        virtual ~Concept() = default;
        virtual Return invoke(Args&&... args) = 0;
    };

    template<typename Function>
    struct Model final : Concept {
        explicit Model(Function function) : function(std::move(function)) {}

        Return invoke(Args&&... args) override {
            return std::invoke(function, std::forward<Args>(args)...);
        }

        Function function;
    };

    std::unique_ptr<Concept> m_function;
};

#endif

} // namespace fei
