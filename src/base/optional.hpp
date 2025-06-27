#pragma once

#include "base/type_traits.hpp"

#include <concepts>
#include <type_traits>

namespace fei {

constexpr struct InPlace {
} in_place;

constexpr struct TrivialInit {
} trivial_init;

constexpr struct NullOpt {
} nullopt;

template<class F, class T>
concept transform_concept = requires {
    requires std::is_object_v<std::invoke_result_t<F, T>>;
    requires !std::is_array_v<std::invoke_result_t<F, T>>;
    requires !is_any_of<std::invoke_result_t<F, T>, NullOpt, InPlace>;
};

template<class T>
union OptionalStorage {
    unsigned char dummy;
    T value;

    OptionalStorage(TrivialInit) : dummy() {}

    template<class... Args>
    OptionalStorage(Args&&... args) : value(std::forward<Args>(args)...) {}
};

template<class T>
class Optional {

    template<class... Args>
    void construct(Args&&... args) {
        new (&m_storage.value) T(std::forward<Args>(args)...);
        m_has_value = true;
    }

  public:
    using value_type = T;

    constexpr Optional() : m_has_value(false), m_storage(trivial_init) {}
    constexpr Optional(NullOpt) : m_has_value(false), m_storage(trivial_init) {}

    constexpr Optional(const T& value) : m_has_value(true), m_storage(value) {}
    constexpr Optional(T&& value) :
        m_has_value(true), m_storage(std::move(value)) {}

    template<class... Args>
    explicit constexpr Optional(InPlace, Args&&... args) :
        m_has_value(true), m_storage(std::forward<Args>(args)...) {}

    Optional(const Optional& other) : m_has_value(other.m_has_value) {
        if (m_has_value) {
            new (&m_storage.value) T(other.m_storage.value);
        }
    }

    Optional(Optional&& other) : m_has_value(other.m_has_value) {
        if (m_has_value) {
            new (&m_storage.value) T(std::move(other.m_storage.value));
            other.m_storage.value.T::~T();
            other.m_has_value = false;
        }
    }

    ~Optional() {
        if (m_has_value) {
            m_storage.value.T::~T();
        }
    }

    Optional& operator=(NullOpt) {
        reset();
        return *this;
    }

    Optional& operator=(const Optional& other) {
        if (m_has_value && other.m_has_value) {
            value() = other.value();
        } else if (m_has_value) {
            reset();
        } else if (other.m_has_value) {
            construct(other.value());
        }
        return *this;
    }

    Optional& operator=(Optional&& other) {
        if (m_has_value && other.m_has_value) {
            value() = std::move(other.value());
        } else if (m_has_value) {
            reset();
        } else if (other.m_has_value) {
            construct(std::move(other.value()));
            other.reset();
        }
        return *this;
    }

    Optional& operator=(const T& value) {
        if (m_has_value) {
            this->value() = value;
        } else {
            construct(value);
        }
        return *this;
    }

    Optional& operator=(T&& value) {
        if (m_has_value) {
            this->value() = std::move(value);
        } else {
            construct(std::move(value));
        }
        return *this;
    }

    constexpr T& value() & { return m_storage.value; }
    constexpr const T& value() const& { return m_storage.value; }
    constexpr T&& value() && { return std::move(m_storage.value); }
    constexpr const T&& value() const&& { return std::move(m_storage.value); }

    constexpr T* operator->() & { return &value(); }
    constexpr const T* operator->() const& { return &value(); }
    constexpr T& operator*() & { return value(); }
    constexpr const T& operator*() const& { return value(); }
    constexpr T&& operator*() && { return std::move(value()); }
    constexpr const T&& operator*() const&& { return std::move(value()); }

    explicit constexpr operator bool() const { return m_has_value; }
    constexpr bool has_value() const { return m_has_value; }

    void reset() {
        if (m_has_value) {
            m_storage.value.T::~T();
            m_has_value = false;
        }
    }

    void swap(Optional& other) {
        if (m_has_value && other.m_has_value) {
            std::swap(**this, *other);
        } else if (m_has_value) {
            other.construct(std::move(**this));
            this->reset();
        } else if (other.m_has_value) {
            this->construct(std::move(*other));
            other.reset();
        }
    }

    template<class... Args>
    void emplace(Args&&... args) {
        reset();
        construct(std::forward<Args>(args)...);
    }

    template<class U>
        requires std::is_convertible_v<U, T>
    constexpr T value_or(U&& default_value) const& {
        return m_has_value ? **this :
                             static_cast<T>(std::forward<U>(default_value));
    }

    template<class U>
        requires std::is_convertible_v<U, T>
    constexpr T value_or(U&& default_value) && {
        return m_has_value ? std::move(**this) :
                             static_cast<T>(std::forward<U>(default_value));
    }

    template<class F>
        requires is_specialization<std::invoke_result_t<F, T&>, Optional>
    constexpr auto and_then(F&& f) & {
        if (*this)
            return std::invoke(std::forward<F>(f), **this);
        else
            return std::remove_cvref_t<std::invoke_result_t<F, T&>> {};
    }
    template<class F>
        requires is_specialization<std::invoke_result_t<F, const T&>, Optional>
    constexpr auto and_then(F&& f) const& {
        if (*this)
            return std::invoke(std::forward<F>(f), **this);
        else
            return std::remove_cvref_t<std::invoke_result_t<F, const T&>> {};
    }
    template<class F>
        requires is_specialization<std::invoke_result_t<F, T>, Optional>
    constexpr auto and_then(F&& f) && {
        if (*this)
            return std::invoke(std::forward<F>(f), std::move(**this));
        else
            return std::remove_cvref_t<std::invoke_result_t<F, T>> {};
    }
    template<class F>
        requires is_specialization<std::invoke_result_t<F, const T>, Optional>
    constexpr auto and_then(F&& f) const&& {
        if (*this)
            return std::invoke(std::forward<F>(f), std::move(**this));
        else
            return std::remove_cvref_t<std::invoke_result_t<F, const T>> {};
    }

    template<class F>
        requires transform_concept<F, T&>
    constexpr auto transform(F&& f) & {
        using U = std::remove_cv_t<std::invoke_result_t<F, T&>>;
        if (*this)
            return Optional<U> {std::invoke(std::forward<F>(f), **this)};
        else
            return Optional<U> {nullopt};
    }

    template<class F>
        requires transform_concept<F, const T&>
    constexpr auto transform(F&& f) const& {
        using U = std::invoke_result_t<F, const T&>;
        if (*this)
            return Optional<U> {std::invoke(std::forward<F>(f), **this)};
        else
            return Optional<U> {nullopt};
    }

    template<class F>
        requires transform_concept<F, T>
    constexpr auto transform(F&& f) && {
        using U = std::invoke_result_t<F, T>;
        if (*this)
            return Optional<U> {
                std::invoke(std::forward<F>(f), std::move(**this))
            };
        else
            return Optional<U> {nullopt};
    }

    template<class F>
        requires transform_concept<F, const T>
    constexpr auto transform(F&& f) const&& {
        using U = std::invoke_result_t<F, const T>;
        if (*this)
            return Optional<U> {
                std::invoke(std::forward<F>(f), std::move(**this))
            };
        else
            return Optional<U> {nullopt};
    }

    template<std::invocable<> F>
        requires std::
            same_as<std::remove_cvref_t<std::invoke_result_t<F>>, Optional>
        constexpr Optional or_else(F&& f) const& {
        if (*this)
            return *this;
        else
            return std::invoke(std::forward<F>(f));
    }

    template<std::invocable<> F>
        requires std::
            same_as<std::remove_cvref_t<std::invoke_result_t<F>>, Optional>
        constexpr Optional or_else(F&& f) && {
        if (*this)
            return std::move(*this);
        else
            return std::invoke(std::forward<F>(f));
    }

  private:
    bool m_has_value;
    OptionalStorage<T> m_storage;
};

template<class T>
class Optional<T&> {
  public:
    constexpr Optional() : m_ref(nullptr) {}
    constexpr Optional(NullOpt) : m_ref(nullptr) {}
    constexpr Optional(T& ref) : m_ref(&ref) {}
    Optional(T&&) = delete;
    Optional(const Optional& other) = delete;
    explicit constexpr Optional(InPlace, T& ref) : m_ref(&ref) {}
    explicit Optional(InPlace, T&&) = delete;
    ~Optional() = default;

    Optional& operator=(NullOpt) {
        m_ref = nullptr;
        return *this;
    }

    Optional& operator=(T& ref) {
        m_ref = &ref;
        return *this;
    }

    void swap(Optional& other) { std::swap(m_ref, other.m_ref); }

    constexpr T& value() const { return *m_ref; }
    constexpr T* operator->() const { return m_ref; }
    constexpr T& operator*() const { return *m_ref; }

    explicit constexpr operator bool() const { return m_ref != nullptr; }
    constexpr bool has_value() const { return m_ref != nullptr; }

    void reset() { m_ref = nullptr; }

    constexpr auto value_or(T& default_value) const {
        return m_ref ? *m_ref : default_value;
    }

    template<class F>
        requires is_specialization<std::invoke_result_t<F, T&>, Optional>
    constexpr auto and_then(F&& f) const {
        if (m_ref) {
            return std::invoke(std::forward<F>(f), *m_ref);
        } else {
            return std::remove_cvref_t<std::invoke_result_t<F, T&>> {};
        }
    }

    template<class F>
        requires transform_concept<F, T&>
    constexpr auto transform(F&& f) const {
        using U = std::remove_cv_t<std::invoke_result_t<F, T&>>;
        if (m_ref) {
            return Optional<U> {std::invoke(std::forward<F>(f), *m_ref)};
        } else {
            return Optional<U> {nullopt};
        }
    }

    template<std::invocable<> F>
        requires std::
            same_as<std::remove_cvref_t<std::invoke_result_t<F>>, Optional>
        constexpr Optional or_else(F&& f) const {
        if (m_ref) {
            return *this;
        } else {
            return std::invoke(std::forward<F>(f));
        }
    }

  private:
    T* m_ref;
};

template<class T>
bool operator==(const Optional<T>& lhs, const Optional<T>& rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    if (!lhs.has_value()) {
        return true;
    }
    return *lhs == *rhs;
}

template<class T>
bool operator!=(const Optional<T>& lhs, const Optional<T>& rhs) {
    return !(lhs == rhs);
}

template<class T>
bool operator==(const Optional<T>& lhs, NullOpt) {
    return !lhs.has_value();
}

template<class T>
bool operator!=(const Optional<T>& lhs, NullOpt) {
    return lhs.has_value();
}

template<class T>
bool operator==(NullOpt, const Optional<T>& rhs) {
    return !rhs.has_value();
}

template<class T>
bool operator!=(NullOpt, const Optional<T>& rhs) {
    return rhs.has_value();
}

} // namespace fei
