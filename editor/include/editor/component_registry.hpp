#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/fwd.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "serialization/node.hpp"
#include "serialization/serializer.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fei {

class World;

namespace editor {

struct ComponentInfo {
    TypeId type;
    std::string name;
};

struct ComponentError {
    enum class Kind {
        NotRegistered,
        EntityNotFound,
        ComponentNotFound,
        TypeNotFound,
        NotDefaultConstructible,
        SerializeFailed,
        DeserializeFailed,
    };

    Kind kind {Kind::NotRegistered};
    Entity entity {};
    TypeId type;
    std::string message;
};

class ComponentRegistry {
  public:
    bool register_component(TypeId type, std::string name);

    template<class T>
    bool register_component(std::string name = {}) {
        auto& type = Registry::instance().register_type<T>();
        if (name.empty()) {
            name = type.stripped_name();
        }
        return register_component(type.id(), std::move(name));
    }

    [[nodiscard]] const ComponentInfo* find(TypeId type) const;
    [[nodiscard]] const std::vector<ComponentInfo>& entries() const {
        return m_entries;
    }

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
    std::vector<ComponentInfo> m_entries;
    serialization::ValueCodecRegistry m_codecs;
};

} // namespace editor
} // namespace fei
