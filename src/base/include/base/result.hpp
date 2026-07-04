#pragma once

#include <concepts>
#include <expected>
#include <functional>
#include <type_traits>
#include <utility>

namespace fei {

template<class T, class E>
class Result : public std::expected<T, E> {
  private:
    using Base = std::expected<T, E>;

  public:
    using Base::Base;

    Result() = default;
    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;

    constexpr Result(const Base& expected) : Base(expected) {}
    constexpr Result(Base&& expected) : Base(std::move(expected)) {}
};

template<class T, class E>
class Result<T&, E> {
  private:
    std::expected<std::reference_wrapper<T>, E> m_expected;

  public:
    using value_type = T&;
    using error_type = E;

    constexpr Result(T& value) : m_expected(std::ref(value)) {}

    template<class G>
        requires std::constructible_from<E, const G&>
    constexpr Result(const std::unexpected<G>& error) :
        m_expected(std::unexpected<E>(error.error())) {}

    template<class G>
        requires std::constructible_from<E, G>
    constexpr Result(std::unexpected<G>&& error) :
        m_expected(std::unexpected<E>(std::move(error.error()))) {}

    constexpr bool has_value() const noexcept { return m_expected.has_value(); }

    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr T& value() const { return m_expected.value().get(); }

    constexpr T& operator*() const noexcept { return m_expected->get(); }

    constexpr T* operator->() const noexcept { return &m_expected->get(); }

    constexpr E& error() & { return m_expected.error(); }

    constexpr const E& error() const& { return m_expected.error(); }

    constexpr E&& error() && { return std::move(m_expected.error()); }

    constexpr const E&& error() const&& {
        return std::move(m_expected.error());
    }
};

template<class E>
using Status = Result<void, E>;

template<class Error>
constexpr std::unexpected<std::remove_cvref_t<Error>> failure(Error&& error) {
    using ErrorType = std::remove_cvref_t<Error>;
    return std::unexpected<ErrorType>(std::forward<Error>(error));
}

} // namespace fei
