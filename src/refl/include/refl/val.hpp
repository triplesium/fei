#pragma once

#include "base/log.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace fei {

class Val {
  public:
    static constexpr std::size_t c_inline_size = sizeof(void*) * 3;
    static constexpr std::size_t c_inline_align = alignof(std::max_align_t);

  private:
    struct alignas(c_inline_align) InlineStorage {
        std::byte bytes[c_inline_size];
    };

    const Type* m_type {nullptr};
    bool m_heap {false};
    void* m_heap_ptr {nullptr};
    InlineStorage m_storage;

    static bool fits_inline(const Type& type) {
        return type.size() <= c_inline_size && type.align() <= c_inline_align;
    }

    void* inline_ptr() { return &m_storage; }
    const void* inline_ptr() const { return &m_storage; }

    void* data_ptr() { return m_heap ? m_heap_ptr : inline_ptr(); }

    const void* data_ptr() const { return m_heap ? m_heap_ptr : inline_ptr(); }

    void* allocate_storage(const Type& type) {
        m_heap = !fits_inline(type);
        if (m_heap) {
            m_heap_ptr =
                ::operator new(type.size(), std::align_val_t {type.align()});
            return m_heap_ptr;
        }
        m_heap_ptr = nullptr;
        return inline_ptr();
    }

    void deallocate_storage() {
        if (m_heap && m_heap_ptr) {
            ::operator delete(
                m_heap_ptr,
                std::align_val_t {m_type ? m_type->align() : c_inline_align}
            );
        }
        m_heap = false;
        m_heap_ptr = nullptr;
    }

    void deallocate_storage(const Type& type) {
        if (m_heap && m_heap_ptr) {
            ::operator delete(m_heap_ptr, std::align_val_t {type.align()});
        }
        m_heap = false;
        m_heap_ptr = nullptr;
    }

    void reset() {
        if (!m_type) {
            return;
        }
        m_type->destroy(data_ptr());
        deallocate_storage();
        m_type = nullptr;
    }

    bool copy_from(const Val& other) {
        if (!other.m_type) {
            return true;
        }
        if (!other.m_type->copy_constructible()) {
            error(
                "Attempting to copy non-copyable type {}",
                other.m_type->name()
            );
            return false;
        }
        m_type = other.m_type;
        void* dest = allocate_storage(*m_type);
        m_type->copy_construct(dest, other.data_ptr());
        return true;
    }

    bool move_from(Val& other) {
        if (!other.m_type) {
            return true;
        }
        if (other.m_type->move_constructible()) {
            m_type = other.m_type;
            void* dest = allocate_storage(*m_type);
            m_type->move_construct(dest, const_cast<void*>(other.data_ptr()));
            other.reset();
            return true;
        }
        if (copy_from(other)) {
            return true;
        }
        error(
            "Attempting to move non-movable and non-copyable type {}",
            other.m_type->name()
        );
        return false;
    }

  public:
    Val() = default;

    ~Val() { reset(); }

    template<class Construct>
    static Val construct(const Type& type, Construct&& construct) {
        Val val;
        void* dest = val.allocate_storage(type);
        try {
            std::forward<Construct>(construct)(dest);
        } catch (...) {
            val.deallocate_storage(type);
            throw;
        }
        val.m_type = &type;
        return val;
    }

    static Val default_construct(const Type& type) {
        return Val::construct(type, [&](void* dest) {
            if (!type.default_construct(dest)) {
                fatal("Cannot default construct Val for type {}", type.name());
            }
        });
    }

    Val(const Val& other) { copy_from(other); }

    Val(Val&& other) noexcept { move_from(other); }

    Val& operator=(const Val& other) {
        if (this == &other) {
            return *this;
        }
        reset();
        copy_from(other);
        return *this;
    }

    Val& operator=(Val&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        reset();
        move_from(other);
        return *this;
    }

    Ref ref() {
        if (!m_type) {
            error("Attempting to get ref from empty Val");
            return {};
        }
        return Ref(data_ptr(), m_type->id());
    }

    Ref ref() const {
        if (!m_type) {
            error("Attempting to get ref from empty Val");
            return {};
        }
        return Ref(data_ptr(), m_type->id());
    }

    TypeId type_id() const { return m_type ? m_type->id() : TypeId {}; }
    const Type* type() const { return m_type; }
    bool empty() const { return m_type == nullptr; }
    explicit operator bool() const { return !empty(); }

    template<typename T>
    T* try_get() {
        return ref().try_get<T>();
    }

    template<typename T>
    const T* try_get() const {
        return ref().try_get_const<T>();
    }

    template<typename T>
    T& get() {
        return ref().get<T>();
    }

    template<typename T>
    const T& get() const {
        return ref().get_const<T>();
    }

    template<typename T>
    T to_number() const {
        if (!m_type) {
            error("Attempting to convert empty Val to number");
            return T(0);
        }
        return ref().to_number<T>();
    }
};

template<class T, class... Args>
Val make_val(Args&&... args) {
    using U = std::remove_cvref_t<T>;
    static_assert(!std::is_reference_v<T>, "Val cannot own a reference type");

    Type& type = Registry::instance().register_type<U>();
    return Val::construct(type, [&](void* dest) {
        if constexpr (requires { U(std::forward<Args>(args)...); }) {
            new (dest) U(std::forward<Args>(args)...);
        } else {
            fatal("Cannot construct Val for type {}", type.name());
        }
    });
}

} // namespace fei
