#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

namespace fei {

struct DynamicMapError {
    enum class Kind {
        InvalidKeyType,
        InvalidMappedType,
        KeyTypeNotFound,
        MappedTypeNotFound,
        KeyNotStorable,
        KeyNotComparable,
        KeyNotHashable,
        InvalidKeyValue,
        MappedValueNotStorable,
        EmptyKey,
        EmptyMappedValue,
        KeyTypeMismatch,
        MappedTypeMismatch,
        KeyNotFound,
        InvalidVisitor,
        VisitorFailed,
    };

    Kind kind;
    TypeId expected_type;
    TypeId actual_type;
    std::string message;
};

struct DynamicMapEntryRef {
    Ref key;
    Ref value;
};

using DynamicMapEntryVisitor = std::function<
    Status<DynamicMapError>(DynamicMapEntryRef entry, std::size_t index)>;

// A runtime-owned homogeneous key-value map. Its key and mapped types are
// selected by create(). Key equality and hashing must obey: equal(a, b)
// implies hash(a) == hash(b). Keys are never exposed as mutable Refs because
// mutating a stored key would invalidate the hash table.
class DynamicMap {
  private:
    struct KeyHash {
        using is_transparent = void;

        std::size_t operator()(const Val& key) const;
        std::size_t operator()(Ref key) const;
    };

    struct KeyEqual {
        using is_transparent = void;

        bool operator()(const Val& lhs, const Val& rhs) const;
        bool operator()(const Val& lhs, Ref rhs) const;
        bool operator()(Ref lhs, const Val& rhs) const;
    };

    using Storage = std::unordered_map<Val, Val, KeyHash, KeyEqual>;

    TypeId m_key_type;
    TypeId m_mapped_type;
    Storage m_entries;

    DynamicMap(TypeId key_type, TypeId mapped_type) :
        m_key_type(key_type), m_mapped_type(mapped_type) {}

  public:
    DynamicMap() = delete;
    ~DynamicMap() = default;

    DynamicMap(const DynamicMap&) = default;
    DynamicMap& operator=(const DynamicMap& other);
    DynamicMap(DynamicMap&&) noexcept = default;
    DynamicMap& operator=(DynamicMap&&) noexcept = default;

    static Result<DynamicMap, DynamicMapError>
    create(TypeId key_type, TypeId mapped_type);

    TypeId key_type() const { return m_key_type; }
    TypeId mapped_type() const { return m_mapped_type; }
    std::size_t size() const { return m_entries.size(); }
    bool empty() const { return m_entries.empty(); }

    Result<Ref, DynamicMapError> find(Ref key);
    Result<Ref, DynamicMapError> find(Ref key) const;
    bool contains(Ref key) const;

    // Any mutation may invalidate every borrowed entry Ref; reacquire them
    // with find() or for_each_entry().
    Status<DynamicMapError> insert_or_assign(Val key, Val value);
    Status<DynamicMapError> insert_or_assign(Ref key, Ref value);
    Status<DynamicMapError> erase(Ref key);

    // Entry enumeration follows the underlying hash-table order and is not
    // stable across processes. Visitors must not structurally modify the map;
    // entry Refs are borrowed and must not outlive their entries.
    Status<DynamicMapError>
    for_each_entry(const DynamicMapEntryVisitor& visitor);
    Status<DynamicMapError>
    for_each_entry(const DynamicMapEntryVisitor& visitor) const;

    // Clearing entries keeps the runtime key and mapped types.
    void clear() { m_entries.clear(); }
    void swap(DynamicMap& other) noexcept;
};

} // namespace fei
