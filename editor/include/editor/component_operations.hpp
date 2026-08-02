#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <string>

namespace fei {

class World;

namespace editor {

struct ComponentError {
    enum class Kind {
        NotComponent,
        EntityNotFound,
        ComponentNotFound,
        TypeNotFound,
        NotDefaultConstructible,
        SerializeFailed,
        DeserializeFailed,
    };

    Kind kind {Kind::NotComponent};
    Entity entity {};
    TypeId type;
    std::string message;
};

class ComponentOperations {
  public:
    [[nodiscard]] serialization::ValueCodecRegistry& codecs() {
        return m_codecs;
    }
    [[nodiscard]] const serialization::ValueCodecRegistry& codecs() const {
        return m_codecs;
    }

    [[nodiscard]] Optional<std::string> preview(Ref value) const;

    Status<ComponentError>
    add_default(World& world, Entity entity, TypeId type) const;
    Status<ComponentError>
    remove(World& world, Entity entity, TypeId type) const;

    Result<serialization::SerializedNode, ComponentError>
    serialize(const World& world, Entity entity, TypeId type) const;
    Status<ComponentError>
    set(World& world,
        Entity entity,
        TypeId type,
        const serialization::SerializedNode& node) const;

  private:
    serialization::ValueCodecRegistry m_codecs;
};

} // namespace editor
} // namespace fei
