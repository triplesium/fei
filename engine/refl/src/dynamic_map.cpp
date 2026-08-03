#include "refl/dynamic_map.hpp"

#include "refl/registry.hpp"

#include <string>
#include <utility>

namespace fei {
namespace {

DynamicMapError make_error(
    DynamicMapError::Kind kind,
    std::string message,
    TypeId expected_type = {},
    TypeId actual_type = {}
) {
    return DynamicMapError {
        .kind = kind,
        .expected_type = expected_type,
        .actual_type = actual_type,
        .message = std::move(message),
    };
}

Status<DynamicMapError> validate_key_type(TypeId key_type) {
    if (!key_type) {
        return failure(make_error(
            DynamicMapError::Kind::InvalidKeyType,
            "DynamicMap key type cannot be empty"
        ));
    }

    auto type = Registry::instance().try_get_type(key_type);
    if (!type) {
        return failure(make_error(
            DynamicMapError::Kind::KeyTypeNotFound,
            type.error().message,
            key_type
        ));
    }
    if (!type->copy_constructible()) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotStorable,
            "DynamicMap key type '" + type->name() +
                "' is not copy constructible",
            key_type
        ));
    }
    if (!type->equality_comparable()) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotComparable,
            "DynamicMap key type '" + type->name() +
                "' is not equality comparable",
            key_type
        ));
    }
    if (!type->hashable()) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotHashable,
            "DynamicMap key type '" + type->name() + "' is not hashable",
            key_type
        ));
    }
    return {};
}

Status<DynamicMapError> validate_mapped_type(TypeId mapped_type) {
    if (!mapped_type) {
        return failure(make_error(
            DynamicMapError::Kind::InvalidMappedType,
            "DynamicMap mapped type cannot be empty"
        ));
    }

    auto type = Registry::instance().try_get_type(mapped_type);
    if (!type) {
        return failure(make_error(
            DynamicMapError::Kind::MappedTypeNotFound,
            type.error().message,
            mapped_type
        ));
    }
    if (!type->copy_constructible()) {
        return failure(make_error(
            DynamicMapError::Kind::MappedValueNotStorable,
            "DynamicMap mapped type '" + type->name() +
                "' is not copy constructible",
            mapped_type
        ));
    }
    return {};
}

std::size_t hash_key(Ref key) {
    if (!key) {
        return 0;
    }
    auto type = Registry::instance().try_get_type(key.type_id());
    if (!type) {
        return 0;
    }
    auto value_hash = type->hash_value(key.const_ptr());
    if (!value_hash) {
        return 0;
    }

    constexpr auto magic = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
    const auto type_hash = static_cast<std::size_t>(key.type_id().id());
    return type_hash ^
           (*value_hash + magic + (type_hash << 6U) + (type_hash >> 2U));
}

bool keys_equal(Ref lhs, Ref rhs) {
    if (!lhs || !rhs || lhs.type_id() != rhs.type_id()) {
        return false;
    }
    auto type = Registry::instance().try_get_type(lhs.type_id());
    if (!type) {
        return false;
    }
    auto equal = type->equals(lhs.const_ptr(), rhs.const_ptr());
    return equal && *equal;
}

Status<DynamicMapError> validate_lookup_key(TypeId expected_type, Ref key) {
    if (!key) {
        return failure(make_error(
            DynamicMapError::Kind::EmptyKey,
            "DynamicMap lookup key cannot be empty",
            expected_type
        ));
    }
    if (key.type_id() != expected_type) {
        return failure(make_error(
            DynamicMapError::Kind::KeyTypeMismatch,
            "DynamicMap key type mismatch",
            expected_type,
            key.type_id()
        ));
    }
    auto type = Registry::instance().try_get_type(expected_type);
    if (!type) {
        return failure(make_error(
            DynamicMapError::Kind::KeyTypeNotFound,
            type.error().message,
            expected_type
        ));
    }
    auto self_equal = type->equals(key.const_ptr(), key.const_ptr());
    if (!self_equal || !*self_equal) {
        return failure(make_error(
            DynamicMapError::Kind::InvalidKeyValue,
            "DynamicMap key must compare equal to itself",
            expected_type,
            key.type_id()
        ));
    }
    return {};
}

} // namespace

std::size_t DynamicMap::KeyHash::operator()(const Val& key) const {
    return hash_key(key.ref());
}

std::size_t DynamicMap::KeyHash::operator()(Ref key) const {
    return hash_key(key);
}

bool DynamicMap::KeyEqual::operator()(const Val& lhs, const Val& rhs) const {
    return keys_equal(lhs.ref(), rhs.ref());
}

bool DynamicMap::KeyEqual::operator()(const Val& lhs, Ref rhs) const {
    return keys_equal(lhs.ref(), rhs);
}

bool DynamicMap::KeyEqual::operator()(Ref lhs, const Val& rhs) const {
    return keys_equal(lhs, rhs.ref());
}

Result<DynamicMap, DynamicMapError>
DynamicMap::create(TypeId key_type, TypeId mapped_type) {
    auto key_status = validate_key_type(key_type);
    if (!key_status) {
        return failure(std::move(key_status.error()));
    }
    auto mapped_status = validate_mapped_type(mapped_type);
    if (!mapped_status) {
        return failure(std::move(mapped_status.error()));
    }

    return DynamicMap(key_type, mapped_type);
}

DynamicMap& DynamicMap::operator=(const DynamicMap& other) {
    if (this == &other) {
        return *this;
    }
    DynamicMap copy(other);
    swap(copy);
    return *this;
}

void DynamicMap::swap(DynamicMap& other) noexcept {
    using std::swap;
    swap(m_key_type, other.m_key_type);
    swap(m_mapped_type, other.m_mapped_type);
    m_entries.swap(other.m_entries);
}

Result<Ref, DynamicMapError> DynamicMap::find(Ref key) {
    auto status = validate_lookup_key(m_key_type, key);
    if (!status) {
        return failure(std::move(status.error()));
    }

    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotFound,
            "DynamicMap key was not found",
            m_key_type,
            key.type_id()
        ));
    }
    return it->second.ref();
}

Result<Ref, DynamicMapError> DynamicMap::find(Ref key) const {
    auto status = validate_lookup_key(m_key_type, key);
    if (!status) {
        return failure(std::move(status.error()));
    }

    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotFound,
            "DynamicMap key was not found",
            m_key_type,
            key.type_id()
        ));
    }
    return it->second.ref();
}

bool DynamicMap::contains(Ref key) const {
    if (!key || key.type_id() != m_key_type) {
        return false;
    }
    return m_entries.find(key) != m_entries.end();
}

Status<DynamicMapError> DynamicMap::insert_or_assign(Val key, Val value) {
    if (!key) {
        return failure(make_error(
            DynamicMapError::Kind::EmptyKey,
            "Cannot insert an empty key into DynamicMap",
            m_key_type
        ));
    }
    if (!value) {
        return failure(make_error(
            DynamicMapError::Kind::EmptyMappedValue,
            "Cannot insert an empty mapped value into DynamicMap",
            m_mapped_type
        ));
    }

    const auto actual_key_type = key.type_id();
    const auto actual_mapped_type = value.type_id();
    if (actual_key_type != m_key_type) {
        return failure(make_error(
            DynamicMapError::Kind::KeyTypeMismatch,
            "DynamicMap key type mismatch",
            m_key_type,
            actual_key_type
        ));
    }
    if (actual_mapped_type != m_mapped_type) {
        return failure(make_error(
            DynamicMapError::Kind::MappedTypeMismatch,
            "DynamicMap mapped type mismatch",
            m_mapped_type,
            actual_mapped_type
        ));
    }

    auto key_value_status = validate_lookup_key(m_key_type, key.ref());
    if (!key_value_status) {
        return key_value_status;
    }

    m_entries.insert_or_assign(std::move(key), std::move(value));
    return {};
}

Status<DynamicMapError> DynamicMap::insert_or_assign(Ref key, Ref value) {
    if (!key) {
        return failure(make_error(
            DynamicMapError::Kind::EmptyKey,
            "Cannot insert an empty key into DynamicMap",
            m_key_type
        ));
    }
    if (!value) {
        return failure(make_error(
            DynamicMapError::Kind::EmptyMappedValue,
            "Cannot insert an empty mapped value into DynamicMap",
            m_mapped_type
        ));
    }
    if (key.type_id() != m_key_type) {
        return failure(make_error(
            DynamicMapError::Kind::KeyTypeMismatch,
            "DynamicMap key type mismatch",
            m_key_type,
            key.type_id()
        ));
    }
    if (value.type_id() != m_mapped_type) {
        return failure(make_error(
            DynamicMapError::Kind::MappedTypeMismatch,
            "DynamicMap mapped type mismatch",
            m_mapped_type,
            value.type_id()
        ));
    }

    auto owned_key = Val::copy(key);
    if (!owned_key) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotStorable,
            owned_key.error().message,
            m_key_type,
            key.type_id()
        ));
    }
    auto owned_value = Val::copy(value);
    if (!owned_value) {
        return failure(make_error(
            DynamicMapError::Kind::MappedValueNotStorable,
            owned_value.error().message,
            m_mapped_type,
            value.type_id()
        ));
    }
    return insert_or_assign(std::move(*owned_key), std::move(*owned_value));
}

Status<DynamicMapError> DynamicMap::erase(Ref key) {
    auto status = validate_lookup_key(m_key_type, key);
    if (!status) {
        return status;
    }

    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return failure(make_error(
            DynamicMapError::Kind::KeyNotFound,
            "DynamicMap key was not found",
            m_key_type,
            key.type_id()
        ));
    }
    m_entries.erase(it);
    return {};
}

Status<DynamicMapError>
DynamicMap::for_each_entry(const DynamicMapEntryVisitor& visitor) {
    if (!visitor) {
        return failure(make_error(
            DynamicMapError::Kind::InvalidVisitor,
            "DynamicMap entry visitor cannot be empty"
        ));
    }

    std::size_t index = 0;
    for (auto& [key, value] : m_entries) {
        auto status = visitor(
            DynamicMapEntryRef {
                .key = key.ref(),
                .value = value.ref(),
            },
            index
        );
        if (!status) {
            return status;
        }
        ++index;
    }
    return {};
}

Status<DynamicMapError>
DynamicMap::for_each_entry(const DynamicMapEntryVisitor& visitor) const {
    if (!visitor) {
        return failure(make_error(
            DynamicMapError::Kind::InvalidVisitor,
            "DynamicMap entry visitor cannot be empty"
        ));
    }

    std::size_t index = 0;
    for (const auto& [key, value] : m_entries) {
        auto status = visitor(
            DynamicMapEntryRef {
                .key = key.ref(),
                .value = value.ref(),
            },
            index
        );
        if (!status) {
            return status;
        }
        ++index;
    }
    return {};
}

} // namespace fei
