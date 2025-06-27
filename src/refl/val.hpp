#pragma once

#include "base/log.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <memory>
#include <type_traits>

namespace fei {

class Val;

namespace detail {
struct ValHandlerBase {
    virtual ~ValHandlerBase() = default;
    virtual Ref ref(const Val& val) const = 0;
    virtual void create(Val& val, Ref ref) const = 0;
    virtual void destroy(Val& val) const = 0;
    virtual void copy(Val& val, const Val& other) const = 0;
    virtual void move(Val& val, Val& other) const = 0;
    virtual bool is_copyable() const = 0;
    virtual bool is_movable() const = 0;
};
template<class T>
struct ValHandlerHeap;
template<class T>
struct ValHandlerStack;
} // namespace detail

class Val {
  public:
    struct Storage {
        uint8_t m_bytes[sizeof(void*) * 3];
        void* get_ptr() const {
            void* ptr = 0;
            std::memcpy(&ptr, m_bytes, sizeof(void*));
            return ptr;
        }
        void set_ptr(void* ptr) { std::memcpy(m_bytes, &ptr, sizeof(void*)); }
    };

  private:
    template<class T>
    friend struct detail::ValHandlerHeap;
    template<class T>
    friend struct detail::ValHandlerStack;

    std::unique_ptr<detail::ValHandlerBase> m_handler;
    Storage m_storage;

  public:
    Val() : m_handler(nullptr) {}
    ~Val() {
        if (m_handler)
            m_handler->destroy(*this);
    }

    template<class T, class... Args>
    friend Val make_val(Args&&... args);

    Val(const Val& other) : Val() {
        if (other.m_handler) {
            if (!other.m_handler->is_copyable()) {
                error("Attempting to copy non-copyable type");
                return;
            }
            other.m_handler->copy(*this, other);
        }
    }

    Val(Val&& other) noexcept : Val() {
        if (other.m_handler) {
            if (other.m_handler->is_movable()) {
                other.m_handler->move(*this, other);
            } else {
                // Fallback to copy if move is not available but copy is
                if (other.m_handler->is_copyable()) {
                    other.m_handler->copy(*this, other);
                } else {
                    error("Attempting to move non-movable and non-copyable type"
                    );
                    return;
                }
            }
        }
    }

    Val& operator=(const Val& other) {
        if (this == &other)
            return *this;

        if (m_handler)
            m_handler->destroy(*this);
        m_handler.reset();

        if (other.m_handler) {
            if (!other.m_handler->is_copyable()) {
                error("Attempting to copy-assign non-copyable type");
                return *this;
            }
            other.m_handler->copy(*this, other);
        }
        return *this;
    }

    Val& operator=(Val&& other) noexcept {
        if (this == &other)
            return *this;

        if (m_handler)
            m_handler->destroy(*this);
        m_handler.reset();

        if (other.m_handler) {
            if (other.m_handler->is_movable()) {
                other.m_handler->move(*this, other);
            } else {
                // Fallback to copy if move is not available but copy is
                if (other.m_handler->is_copyable()) {
                    other.m_handler->copy(*this, other);
                } else {
                    error("Attempting to move-assign non-movable and "
                          "non-copyable type");
                    return *this;
                }
            }
        }
        return *this;
    }

    Val& swap(Val& other) {
        std::swap(m_handler, other.m_handler);
        std::swap(m_storage, other.m_storage);
        return *this;
    }

    Ref ref() const {
        if (!m_handler) {
            error("Attempting to get ref from empty Val");
            return {};
        }
        return m_handler->ref(*this);
    }
    TypeId type_id() const { return ref().type_id(); }
    bool empty() const { return m_handler == nullptr; }
    explicit operator bool() const { return !empty(); }

    template<class T>
    T& get() {
        return ref().get<T>();
    }
    template<class T>
    const T& get() const {
        return ref().get<T>();
    }
};

namespace detail {
template<class T>
struct ValHandlerHeap : public ValHandlerBase {
    virtual Ref ref(const Val& val) const override {
        return make_ref(static_cast<const T*>(val.m_storage.get_ptr()));
    }
    virtual void create(Val& val, Ref ref) const override {
        if constexpr (std::copy_constructible<T>) {
            val.m_storage.set_ptr(new T(ref.get<T>()));
        } else {
            error(
                "Cannot create heap object from reference for non-copyable type"
            );
        }
    }
    template<class... Args>
        requires std::constructible_from<T, Args...>
    void construct(Val& val, Args&&... args) const {
        val.m_storage.set_ptr(new T(std::forward<Args>(args)...));
    }
    virtual void destroy(Val& val) const override {
        delete static_cast<T*>(val.m_storage.get_ptr());
    }
    virtual void copy(Val& val, const Val& other) const override {
        if constexpr (std::copy_constructible<T>) {
            create(val, ref(other));
            val.m_handler = std::make_unique<ValHandlerHeap<T>>();
        } else {
            error("Cannot copy non-copyable type");
        }
    }
    virtual void move(Val& val, Val& other) const override {
        if constexpr (std::move_constructible<T>) {
            // Actually move the object, not just swap the containers
            T* other_ptr = static_cast<T*>(other.m_storage.get_ptr());
            val.m_storage.set_ptr(new T(std::move(*other_ptr)));
            val.m_handler = std::make_unique<ValHandlerHeap<T>>();

            // Clear the other Val
            other.m_handler->destroy(other);
            other.m_handler.reset();
        } else if constexpr (std::copy_constructible<T>) {
            // Fallback to copy
            copy(val, other);
        } else {
            error("Cannot move non-movable and non-copyable type");
        }
    }
    virtual bool is_copyable() const override {
        return std::copy_constructible<T>;
    }
    virtual bool is_movable() const override {
        return std::move_constructible<T>;
    }
};
template<class T>
struct ValHandlerStack : public ValHandlerBase {
    virtual Ref ref(const Val& val) const override {
        return make_ref(static_cast<const T*>((void*)&val.m_storage));
    }
    virtual void create(Val& val, Ref ref) const override {
        if constexpr (std::copy_constructible<T>) {
            new ((void*)&val.m_storage) T(ref.get<T>());
        } else {
            error("Cannot create stack object from reference for non-copyable "
                  "type");
        }
    }
    template<class... Args>
        requires std::constructible_from<
            T,
            decltype(std::forward<Args>(std::declval<Args>()))...>
    void construct(Val& val, Args&&... args) const {
        new ((void*)&val.m_storage) T(std::forward<Args>(args)...);
    }
    virtual void destroy(Val& val) const override {
        static_cast<T*>((void*)&val.m_storage)->~T();
    }
    virtual void copy(Val& val, const Val& other) const override {
        if constexpr (std::copy_constructible<T>) {
            create(val, ref(other));
            val.m_handler = std::make_unique<ValHandlerStack<T>>();
        } else {
            error("Cannot copy non-copyable type");
        }
    }
    virtual void move(Val& val, Val& other) const override {
        if constexpr (std::move_constructible<T>) {
            // Actually move the object, not just swap the containers
            T* other_ptr = static_cast<T*>((void*)&other.m_storage);
            new ((void*)&val.m_storage) T(std::move(*other_ptr));
            val.m_handler = std::make_unique<ValHandlerStack<T>>();

            // Clear the other Val
            other.m_handler->destroy(other);
            other.m_handler.reset();
        } else if constexpr (std::copy_constructible<T>) {
            // Fallback to copy
            copy(val, other);
        } else {
            error("Cannot move non-movable and non-copyable type");
        }
    }
    virtual bool is_copyable() const override {
        return std::copy_constructible<T>;
    }
    virtual bool is_movable() const override {
        return std::move_constructible<T>;
    }
};
} // namespace detail

template<class T>
concept small_object = sizeof(T) <= sizeof(void*) * 3;

template<class T>
using ValHandler = std::conditional_t<
    small_object<std::decay_t<T>>,
    detail::ValHandlerStack<std::decay_t<T>>,
    detail::ValHandlerHeap<std::decay_t<T>>>;

template<class T, class... Args>
Val make_val(Args&&... args) {
    Val val;
    auto handler = std::make_unique<ValHandler<T>>();
    handler->construct(val, std::forward<Args>(args)...);
    val.m_handler = std::move(handler);
    return val;
}

} // namespace fei
