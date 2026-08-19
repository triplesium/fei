#pragma once

#include "base/optional.hpp"
#include "refl/utils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

constexpr std::uint64_t stable_name_hash(std::string_view name) {
    std::uint64_t hash = 14695981039346656037ull;
    for (char c : name) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    hash &= 0x7fffffffffffffffull;
    return hash == 0 ? 1 : hash;
}

constexpr std::uint64_t stable_type_hash(std::string_view name) {
    return stable_name_hash(name);
}

class TypeId {
  private:
    std::uint64_t m_id;

  public:
    TypeId() : m_id(0) {}
    TypeId(std::uint64_t id) : m_id(id) {}
    TypeId(std::string_view name) : m_id(stable_type_hash(name)) {}
    TypeId(const std::string& name) : TypeId(std::string_view {name}) {}

    std::uint64_t id() const { return m_id; }

    auto operator<=>(const TypeId& other) const { return m_id <=> other.m_id; }
    bool operator==(const TypeId& other) const { return m_id == other.m_id; }

    operator std::uint64_t() const { return m_id; }
    operator bool() const { return m_id != 0; }
};

class TypeTagId {
  private:
    std::uint64_t m_id;

  public:
    constexpr TypeTagId() : m_id(0) {}
    constexpr explicit TypeTagId(std::uint64_t id) : m_id(id) {}
    constexpr explicit TypeTagId(std::string_view name) :
        m_id(stable_name_hash(name)) {}

    constexpr std::uint64_t id() const { return m_id; }

    constexpr auto operator<=>(const TypeTagId& other) const {
        return m_id <=> other.m_id;
    }
    constexpr bool operator==(const TypeTagId& other) const {
        return m_id == other.m_id;
    }

    constexpr explicit operator bool() const { return m_id != 0; }
};

template<typename T>
inline TypeId type_id() {
    return TypeId(type_name<std::remove_cvref_t<T>>());
}

struct TypeOps {
    using DefaultConstructFunc = void (*)(const void* context, void* dest);
    using CopyConstructFunc =
        void (*)(const void* context, void* dest, const void* src);
    // Unsafe C++ move operations are represented by a null callback. Exposed
    // move and destruction callbacks form a non-throwing relocation boundary.
    using MoveConstructFunc =
        void (*)(const void* context, void* dest, void* src) noexcept;
    using DestroyFunc = void (*)(const void* context, void* ptr) noexcept;
    using CopyAssignFunc =
        bool (*)(const void* context, void* dest, const void* src);
    using MoveAssignFunc =
        bool (*)(const void* context, void* dest, void* src) noexcept;
    using EqualFunc =
        bool (*)(const void* context, const void* lhs, const void* rhs);
    // Runtime hash for in-process lookup. It is not a persistent or stable ID.
    using HashValueFunc =
        std::size_t (*)(const void* context, const void* value);

    const void* context {nullptr};
    std::shared_ptr<const void> context_owner;
    DefaultConstructFunc default_construct {nullptr};
    CopyConstructFunc copy_construct {nullptr};
    MoveConstructFunc move_construct {nullptr};
    DestroyFunc destroy {nullptr};
    CopyAssignFunc copy_assign {nullptr};
    MoveAssignFunc move_assign {nullptr};
    EqualFunc equal {nullptr};
    HashValueFunc hash_value {nullptr};
};

struct AnnotationField {
    std::string name;
    std::string value;
};

class AnnotationView {
  private:
    std::string_view m_name;
    std::span<const AnnotationField> m_fields;

  public:
    AnnotationView(
        std::string_view name,
        std::span<const AnnotationField> fields
    ) : m_name(name), m_fields(fields) {}

    std::string_view name() const { return m_name; }
    std::span<const AnnotationField> fields() const { return m_fields; }

    Optional<std::string_view> value(std::string_view field) const {
        const auto found =
            std::ranges::find(m_fields, field, &AnnotationField::name);
        if (found == m_fields.end()) {
            return nullopt;
        }
        return std::string_view {found->value};
    }
};

struct Annotation {
    std::string name;
    std::vector<AnnotationField> fields;
};

class Type {
  public:
    using DefaultConstructFunc = TypeOps::DefaultConstructFunc;
    using CopyConstructFunc = TypeOps::CopyConstructFunc;
    using MoveConstructFunc = TypeOps::MoveConstructFunc;
    using DeleteFunc = TypeOps::DestroyFunc;
    using DestroyFunc = TypeOps::DestroyFunc;
    using CopyAssignFunc = TypeOps::CopyAssignFunc;
    using MoveAssignFunc = TypeOps::MoveAssignFunc;
    using EqualFunc = TypeOps::EqualFunc;
    using HashValueFunc = TypeOps::HashValueFunc;

  private:
    std::string m_name;
    std::vector<std::string> m_namespace_path;
    std::string m_local_name;
    bool m_has_structured_name {false};
    TypeId m_id;
    std::size_t m_size;
    std::size_t m_align;
    TypeOps m_ops;
    std::vector<TypeTagId> m_tags;
    std::vector<std::pair<TypeTagId, std::string>> m_tag_values;
    std::vector<Annotation> m_annotations;

    void add_tag(TypeTagId tag) {
        auto position = std::ranges::lower_bound(m_tags, tag);
        if (position == m_tags.end() || *position != tag) {
            m_tags.insert(position, tag);
        }
    }

    void set_tag_value(TypeTagId tag, std::string value) {
        auto position = std::ranges::lower_bound(
            m_tag_values,
            tag,
            {},
            &std::pair<TypeTagId, std::string>::first
        );
        if (position == m_tag_values.end() || position->first != tag) {
            m_tag_values.insert(position, {tag, std::move(value)});
        } else {
            position->second = std::move(value);
        }
    }

    Annotation& add_annotation(std::string name) {
        auto position = std::ranges::lower_bound(
            m_annotations,
            name,
            {},
            &Annotation::name
        );
        if (position == m_annotations.end() || position->name != name) {
            position = m_annotations.insert(
                position,
                Annotation {.name = std::move(name)}
            );
        }
        return *position;
    }

    void set_annotation_field(
        std::string annotation,
        std::string field,
        std::string value
    ) {
        auto& fields = add_annotation(std::move(annotation)).fields;
        auto position =
            std::ranges::lower_bound(fields, field, {}, &AnnotationField::name);
        if (position == fields.end() || position->name != field) {
            fields.insert(
                position,
                AnnotationField {
                    .name = std::move(field),
                    .value = std::move(value),
                }
            );
        } else {
            position->value = std::move(value);
        }
    }

    void clear_tags() {
        m_tags.clear();
        m_tag_values.clear();
        m_annotations.clear();
    }

    friend class Registry;

  public:
    Type(
        std::string name,
        TypeId id,
        std::size_t size,
        std::size_t align,
        TypeOps ops
    ) :
        m_name(std::move(name)), m_id(id), m_size(size), m_align(align),
        m_ops(ops) {}

    const std::string& name() const { return m_name; }
    std::span<const std::string> namespace_path() const {
        return m_namespace_path;
    }
    std::string_view local_name() const {
        return m_has_structured_name ? std::string_view {m_local_name} :
                                       std::string_view {m_name};
    }
    bool has_structured_name() const { return m_has_structured_name; }
    TypeId hash() const { return m_id; }
    TypeId id() const { return m_id; }
    std::size_t size() const { return m_size; }
    std::size_t align() const { return m_align; }
    bool is_number() const;
    bool is_integral() const;
    bool is_floating_point() const;
    std::string stripped_name() const;
    const TypeOps& ops() const { return m_ops; }
    DefaultConstructFunc default_construct_func() const {
        return m_ops.default_construct;
    }
    CopyConstructFunc copy_construct_func() const {
        return m_ops.copy_construct;
    }
    MoveConstructFunc move_construct_func() const {
        return m_ops.move_construct;
    }
    DeleteFunc delete_func() const { return m_ops.destroy; }
    DestroyFunc destroy_func() const { return m_ops.destroy; }
    CopyAssignFunc copy_assign_func() const { return m_ops.copy_assign; }
    MoveAssignFunc move_assign_func() const { return m_ops.move_assign; }
    EqualFunc equal_func() const { return m_ops.equal; }
    HashValueFunc hash_value_func() const { return m_ops.hash_value; }

    bool default_construct(void* dest) const {
        if (!m_ops.default_construct) {
            return false;
        }
        m_ops.default_construct(m_ops.context, dest);
        return true;
    }
    bool copy_construct(void* dest, const void* src) const {
        if (!m_ops.copy_construct) {
            return false;
        }
        m_ops.copy_construct(m_ops.context, dest, src);
        return true;
    }
    bool move_construct(void* dest, void* src) const noexcept {
        if (!m_ops.move_construct) {
            return false;
        }
        m_ops.move_construct(m_ops.context, dest, src);
        return true;
    }
    bool destroy(void* ptr) const noexcept {
        if (!m_ops.destroy) {
            return false;
        }
        m_ops.destroy(m_ops.context, ptr);
        return true;
    }
    bool copy_assign(void* dest, const void* src) const {
        if (!m_ops.copy_assign) {
            return false;
        }
        return m_ops.copy_assign(m_ops.context, dest, src);
    }
    bool move_assign(void* dest, void* src) const noexcept {
        if (!m_ops.move_assign) {
            return false;
        }
        return m_ops.move_assign(m_ops.context, dest, src);
    }

    Optional<bool> equals(const void* lhs, const void* rhs) const {
        if (!m_ops.equal) {
            return nullopt;
        }
        return m_ops.equal(m_ops.context, lhs, rhs);
    }

    Optional<std::size_t> hash_value(const void* value) const {
        if (!m_ops.hash_value) {
            return nullopt;
        }
        return m_ops.hash_value(m_ops.context, value);
    }

    bool default_constructible() const {
        return m_ops.default_construct != nullptr;
    }
    bool copy_constructible() const { return m_ops.copy_construct != nullptr; }
    bool move_constructible() const { return m_ops.move_construct != nullptr; }
    bool copy_assignable() const { return m_ops.copy_assign != nullptr; }
    bool move_assignable() const { return m_ops.move_assign != nullptr; }
    bool destructible() const { return m_ops.destroy != nullptr; }
    bool equality_comparable() const { return m_ops.equal != nullptr; }
    bool hashable() const { return m_ops.hash_value != nullptr; }

    bool has_tag(TypeTagId tag) const {
        return std::ranges::binary_search(m_tags, tag);
    }
    Optional<std::string_view> tag_value(TypeTagId tag) const {
        auto value = std::ranges::lower_bound(
            m_tag_values,
            tag,
            {},
            &std::pair<TypeTagId, std::string>::first
        );
        if (value == m_tag_values.end() || value->first != tag) {
            return nullopt;
        }
        return std::string_view {value->second};
    }
    std::span<const TypeTagId> tags() const { return m_tags; }

    bool has_annotation(std::string_view name) const {
        return annotation(name).has_value();
    }

    Optional<AnnotationView> annotation(std::string_view name) const {
        const auto found = std::ranges::lower_bound(
            m_annotations,
            name,
            {},
            &Annotation::name
        );
        if (found == m_annotations.end() || found->name != name) {
            return nullopt;
        }
        return AnnotationView {found->name, found->fields};
    }

    std::span<const Annotation> annotations() const { return m_annotations; }

    auto operator<=>(const Type& other) const { return m_id <=> other.m_id; }
};
} // namespace fei

namespace std {
template<>
struct hash<fei::TypeId> { // NOLINT(readability-identifier-naming)
    size_t operator()(const fei::TypeId& id) const {
        return static_cast<size_t>(id.id());
    }
};
template<>
struct hash<fei::Type> { // NOLINT(readability-identifier-naming)
    size_t operator()(const fei::Type& type) const {
        return static_cast<size_t>(type.hash().id());
    }
};

template<>
struct hash<fei::TypeTagId> { // NOLINT(readability-identifier-naming)
    size_t operator()(const fei::TypeTagId& id) const {
        return static_cast<size_t>(id.id());
    }
};

} // namespace std
