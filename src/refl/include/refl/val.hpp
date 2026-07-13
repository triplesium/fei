#pragma once

#include "base/log.hpp"
#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace fei {

struct ValError {
    enum class Kind {
        EmptySource,
        TypeNotFound,
        NotCopyConstructible,
    };

    Kind kind;
    TypeId type_id;
    std::string message;
};

class Val {
  public:
    static constexpr std::size_t c_inline_size = sizeof(void*) * 3;
    static constexpr std::size_t c_inline_align = alignof(std::max_align_t);

  private:
    struct alignas(c_inline_align) InlineStorage {
        std::byte bytes[c_inline_size];
    };

    struct AlignedStorageDeleter {
        std::size_t alignment {c_inline_align};

        void operator()(std::byte* ptr) const noexcept {
            ::operator delete(ptr, std::align_val_t {alignment});
        }
    };

    using HeapStorage = std::unique_ptr<std::byte, AlignedStorageDeleter>;

    const Type* m_type {nullptr};
    HeapStorage m_heap {nullptr, AlignedStorageDeleter {}};
    InlineStorage m_storage;

    static bool fits_inline(const Type& type) {
        // A payload without reflected noexcept move stays on the heap so
        // moving Val only transfers its allocation.
        return type.move_constructible() && type.size() <= c_inline_size &&
               type.align() <= c_inline_align;
    }

    void* inline_ptr() { return &m_storage; }
    const void* inline_ptr() const { return &m_storage; }

    void* data_ptr() { return m_heap ? m_heap.get() : inline_ptr(); }

    const void* data_ptr() const {
        return m_heap ? m_heap.get() : inline_ptr();
    }

    void* allocate_storage(const Type& type) {
        if (fits_inline(type)) {
            m_heap.reset();
            return inline_ptr();
        }

        HeapStorage storage {
            static_cast<std::byte*>(
                ::operator new(type.size(), std::align_val_t {type.align()})
            ),
            AlignedStorageDeleter {.alignment = type.align()},
        };
        auto* result = storage.get();
        m_heap = std::move(storage);
        return result;
    }

    void reset() noexcept {
        if (m_type) {
            m_type->destroy(data_ptr());
            m_type = nullptr;
        }
        m_heap.reset();
    }

    void copy_from(const Val& other) {
        if (!other.m_type) {
            return;
        }
        if (!other.m_type->copy_constructible()) {
            throw std::logic_error(
                "Cannot copy Val containing non-copyable type '" +
                other.m_type->name() + "'"
            );
        }
        const Type* type = other.m_type;
        void* dest = allocate_storage(*type);
        type->copy_construct(dest, other.data_ptr());
        m_type = type;
    }

    void move_from(Val& other) noexcept {
        if (!other.m_type) {
            return;
        }

        const Type* type = other.m_type;
        if (other.m_heap) {
            m_type = std::exchange(other.m_type, nullptr);
            m_heap = std::move(other.m_heap);
            return;
        }

        if (!type->move_construct(inline_ptr(), other.inline_ptr())) {
            std::terminate();
        }
        m_type = type;
        other.reset();
    }

  public:
    Val() = default;

    ~Val() noexcept { reset(); }

    template<class Construct>
    static Val construct(const Type& type, Construct&& construct) {
        if (!type.destructible()) {
            fatal("Cannot store type '{}' in Val", type.name());
        }
        Val val;
        void* dest = val.allocate_storage(type);
        std::forward<Construct>(construct)(dest);
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

    static Result<Val, ValError> copy(Ref source) {
        if (!source) {
            return failure(
                ValError {
                    .kind = ValError::Kind::EmptySource,
                    .message = "Cannot copy an empty Ref into Val",
                }
            );
        }

        auto type = Registry::instance().try_get_type(source.type_id());
        if (!type) {
            return failure(
                ValError {
                    .kind = ValError::Kind::TypeNotFound,
                    .type_id = source.type_id(),
                    .message = type.error().message,
                }
            );
        }
        if (!type->copy_constructible()) {
            return failure(
                ValError {
                    .kind = ValError::Kind::NotCopyConstructible,
                    .type_id = source.type_id(),
                    .message =
                        "Type '" + type->name() + "' is not copy constructible",
                }
            );
        }
        return Val::construct(*type, [&](void* dest) {
            type->copy_construct(dest, source.const_ptr());
        });
    }

    Val(const Val& other) { copy_from(other); }

    Val(Val&& other) noexcept { move_from(other); }

    Val& operator=(const Val& other) {
        if (this == &other) {
            return *this;
        }
        Val copy(other);
        swap(copy);
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

    void swap(Val& other) noexcept {
        if (this == &other) {
            return;
        }
        Val temp(std::move(other));
        other = std::move(*this);
        *this = std::move(temp);
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
    static_assert(
        std::is_nothrow_destructible_v<U>,
        "Val payloads must be nothrow destructible"
    );

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
