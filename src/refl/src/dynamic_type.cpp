#include "refl/dynamic_type.hpp"

#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <algorithm>
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

void dynamic_default_construct(const void* context, void* dest) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        void* ptr = field_ptr(dest, field.offset);
        type.default_construct(ptr);
        if (field.default_value) {
            type.destroy(ptr);
            auto default_ref = field.default_value->ref();
            type.copy_construct(ptr, default_ref.const_ptr());
        }
    }
}

void dynamic_copy_construct(const void* context, void* dest, const void* src) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        type.copy_construct(
            field_ptr(dest, field.offset),
            field_ptr(src, field.offset)
        );
    }
}

void dynamic_move_construct(const void* context, void* dest, void* src) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        void* dest_field = field_ptr(dest, field.offset);
        void* src_field = field_ptr(src, field.offset);
        if (!type.move_construct(dest_field, src_field)) {
            type.copy_construct(dest_field, src_field);
        }
    }
}

void dynamic_destroy(const void* context, void* ptr) {
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

bool dynamic_move_assign(const void* context, void* dest, void* src) {
    const auto& layout = *static_cast<const DynamicStructLayout*>(context);
    for (const auto& field : layout.fields) {
        const auto& type = field_type(field.type);
        void* dest_field = field_ptr(dest, field.offset);
        void* src_field = field_ptr(src, field.offset);
        if (!type.move_assign(dest_field, src_field) &&
            !type.copy_assign(dest_field, src_field)) {
            return false;
        }
    }
    return true;
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
        if (type->size() == 0 || type->align() == 0 ||
            !type->default_constructible() || !type->copy_constructible() ||
            !type->destructible()) {
            return failure(dynamic_type_error(
                DynamicTypeError::Kind::InvalidFieldType,
                "Field '" + field_desc.name + "' type '" + type->name() +
                    "' cannot be stored in a dynamic struct"
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
    ops.move_construct = &dynamic_move_construct;
    ops.destroy = &dynamic_destroy;
    ops.copy_assign = &dynamic_copy_assign;
    ops.move_assign = &dynamic_move_assign;
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
