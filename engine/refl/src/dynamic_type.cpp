#include "refl/dynamic_type.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace fei {
namespace {

DynamicTypeError
dynamic_type_error(DynamicTypeError::Kind kind, std::string message) {
    return DynamicTypeError {
        .kind = kind,
        .message = std::move(message),
    };
}

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    const auto remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + alignment - remainder;
}

void* field_ptr(void* object, std::size_t offset) {
    return static_cast<std::byte*>(object) + offset;
}

const void* field_ptr(const void* object, std::size_t offset) {
    return static_cast<const std::byte*>(object) + offset;
}

const Type& field_type(TypeId id) {
    return Registry::instance().get_type(id);
}

void destroy_constructed_fields(
    const DynamicStructLayout& layout,
    void* object,
    std::size_t count
) noexcept {
    while (count > 0) {
        const auto& field = layout.fields[--count];
        field_type(field.type).destroy(field_ptr(object, field.offset));
    }
}

void dynamic_default_construct(const void* context, void* dest) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    std::size_t constructed = 0;
    try {
        for (const auto& field : layout.fields) {
            const auto& type = field_type(field.type);
            void* ptr = field_ptr(dest, field.offset);
            if (field.default_value) {
                auto default_ref = field.default_value->ref();
                type.copy_construct(ptr, default_ref.const_ptr());
            } else {
                type.default_construct(ptr);
            }
            ++constructed;
        }
    } catch (...) {
        destroy_constructed_fields(layout, dest, constructed);
        throw;
    }
}

void dynamic_copy_construct(const void* context, void* dest, const void* src) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    std::size_t constructed = 0;
    try {
        for (const auto& field : layout.fields) {
            const auto& type = field_type(field.type);
            type.copy_construct(
                field_ptr(dest, field.offset),
                field_ptr(src, field.offset)
            );
            ++constructed;
        }
    } catch (...) {
        destroy_constructed_fields(layout, dest, constructed);
        throw;
    }
}

void dynamic_move_construct(
    const void* context,
    void* dest,
    void* src
) noexcept {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        const bool moved = type.move_construct(
            field_ptr(dest, field.offset),
            field_ptr(src, field.offset)
        );
        if (!moved) {
            std::terminate();
        }
    }
}

void dynamic_destroy(const void* context, void* ptr) noexcept {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (auto it = layout.fields.rbegin(); it != layout.fields.rend(); ++it) {
        const auto& type = field_type(it->type);
        type.destroy(field_ptr(ptr, it->offset));
    }
}

bool dynamic_copy_assign(const void* context, void* dest, const void* src) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        if (!type.copy_assign(
                field_ptr(dest, field.offset),
                field_ptr(src, field.offset)
            )) {
            return false;
        }
    }
    return true;
}

bool dynamic_move_assign(const void* context, void* dest, void* src) noexcept {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        void* dest_field = field_ptr(dest, field.offset);
        void* src_field = field_ptr(src, field.offset);
        if (!type.move_assign(dest_field, src_field)) {
            return false;
        }
    }
    return true;
}

bool dynamic_equal(const void* context, const void* lhs, const void* rhs) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        auto equal = type.equals(
            field_ptr(lhs, field.offset),
            field_ptr(rhs, field.offset)
        );
        if (!equal || !*equal) {
            return false;
        }
    }
    return true;
}

std::size_t combine_hash(std::size_t seed, std::size_t value) {
    constexpr auto magic = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
    return seed ^ (value + magic + (seed << 6U) + (seed >> 2U));
}

std::size_t dynamic_hash_value(const void* context, const void* value) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    std::size_t hash = 0;
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        auto field_hash = type.hash_value(field_ptr(value, field.offset));
        if (!field_hash) {
            return 0;
        }
        hash = combine_hash(hash, *field_hash);
    }
    return hash;
}

Result<DynamicStructLayout, DynamicTypeError>
build_dynamic_struct_layout(Registry& registry, DynamicStructDesc desc) {
    if (!desc.id || desc.name.empty()) {
        return failure(dynamic_type_error(
            DynamicTypeError::Kind::InvalidFieldType,
            "Dynamic struct must have a non-empty name and TypeId"
        ));
    }

    std::unordered_set<std::string> field_names;
    DynamicStructLayout layout {
        .name = std::move(desc.name),
        .id = desc.id,
    };

    std::size_t offset = 0;
    for (auto& field_desc : desc.fields) {
        if (field_desc.name.empty()) {
            return failure(dynamic_type_error(
                DynamicTypeError::Kind::InvalidFieldType,
                "Dynamic struct field names must be non-empty"
            ));
        }
        if (!field_names.insert(field_desc.name).second) {
            return failure(dynamic_type_error(
                DynamicTypeError::Kind::DuplicateField,
                "Duplicate dynamic struct field '" + field_desc.name + "'"
            ));
        }

        auto type = registry.try_get_type(field_desc.type);
        if (!type) {
            return failure(dynamic_type_error(
                DynamicTypeError::Kind::FieldTypeNotFound,
                type.error().message
            ));
        }
        if (field_desc.default_value &&
            field_desc.default_value->type_id() != field_desc.type) {
            return failure(dynamic_type_error(
                DynamicTypeError::Kind::InvalidFieldType,
                "Default value for dynamic struct field '" + field_desc.name +
                    "' has the wrong type"
            ));
        }
        const bool initializable =
            type->default_constructible() || field_desc.default_value;
        if (type->size() == 0 || type->align() == 0 || !initializable ||
            !type->copy_constructible() || !type->destructible()) {
            return failure(dynamic_type_error(
                DynamicTypeError::Kind::InvalidFieldType,
                "Field '" + field_desc.name + "' type '" + type->name() +
                    "' cannot be stored in a dynamic struct"
            ));
        }

        layout.align = std::max(layout.align, type->align());
        offset = align_to(offset, type->align());
        layout.fields.push_back(
            DynamicField {
                .name = std::move(field_desc.name),
                .type = field_desc.type,
                .offset = offset,
                .default_value = std::move(field_desc.default_value),
            }
        );
        offset += type->size();
    }

    layout.size = std::max<std::size_t>(1, align_to(offset, layout.align));
    return layout;
}

TypeOps
make_dynamic_struct_ops(std::shared_ptr<const DynamicStructLayout> layout) {
    TypeOps ops;
    ops.context = layout.get();
    ops.context_owner = layout;
    ops.default_construct = &dynamic_default_construct;
    ops.copy_construct = &dynamic_copy_construct;
    ops.destroy = &dynamic_destroy;

    bool move_constructible = true;
    bool copy_assignable = true;
    bool move_assignable = true;
    bool equality_comparable = true;
    bool hashable = true;
    for (const auto& field : layout->fields) {
        const auto& type = field_type(field.type);
        move_constructible &= type.move_constructible();
        copy_assignable &= type.copy_assignable();
        move_assignable &= type.move_assignable();
        equality_comparable &= type.equality_comparable();
        hashable &= type.hashable();
    }
    if (move_constructible) {
        ops.move_construct = &dynamic_move_construct;
    }
    if (copy_assignable) {
        ops.copy_assign = &dynamic_copy_assign;
    }
    if (move_assignable) {
        ops.move_assign = &dynamic_move_assign;
    }
    if (equality_comparable) {
        ops.equal = &dynamic_equal;
    }
    if (hashable) {
        ops.hash_value = &dynamic_hash_value;
    }
    return ops;
}

void install_dynamic_struct_class(
    Registry& registry,
    const DynamicStructLayout& layout
) {
    auto& cls = registry.add_cls(layout.id);
    for (const auto& field : layout.fields) {
        cls.add_offset_property(field.name, field.type, field.offset);
    }
}

} // namespace

Result<Type&, DynamicTypeError>
Registry::register_dynamic_struct(DynamicStructDesc desc) {
    if (auto existing = try_get_type(desc.id)) {
        return failure(dynamic_type_error(
            DynamicTypeError::Kind::TypeAlreadyExists,
            "Dynamic struct TypeId already exists for '" + existing->name() +
                "'"
        ));
    }

    auto layout_result = build_dynamic_struct_layout(*this, std::move(desc));
    if (!layout_result) {
        return failure(std::move(layout_result.error()));
    }

    auto layout =
        std::make_shared<DynamicStructLayout>(std::move(*layout_result));
    auto ops = make_dynamic_struct_ops(layout);
    auto& type = register_type(
        layout->id,
        layout->name,
        layout->size,
        layout->align,
        std::move(ops)
    );
    m_dynamic_structs.emplace(layout->id, layout);
    install_dynamic_struct_class(*this, *layout);
    return type;
}

const DynamicStructLayout*
Registry::try_get_dynamic_struct_layout(TypeId id) const {
    auto it = m_dynamic_structs.find(id);
    if (it == m_dynamic_structs.end()) {
        return nullptr;
    }
    return it->second.get();
}

} // namespace fei
