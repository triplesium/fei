#pragma once

#include "base/log.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <type_traits>
#include <unordered_map>
#include <utility>

namespace fei {

class Resources {
  private:
    struct ResourceEntry {
        Val value;
        Ref (*ref)(Val&) {nullptr};
        Ref (*const_ref)(const Val&) {nullptr};
    };

    template<typename Exposed, typename Stored>
    static Ref make_resource_ref(Val& value) {
        auto* stored = value.try_get<Stored>();
        if (!stored) {
            return {};
        }
        return Ref(static_cast<Exposed*>(stored), type_id<Exposed>());
    }

    template<typename Exposed, typename Stored>
    static Ref make_resource_const_ref(const Val& value) {
        auto* stored = value.try_get<Stored>();
        if (!stored) {
            return {};
        }
        return Ref(static_cast<const Exposed*>(stored), type_id<Exposed>());
    }

    static Ref make_val_ref(Val& value) { return value.ref(); }

    static Ref make_val_const_ref(const Val& value) { return value.ref(); }

    std::unordered_map<TypeId, ResourceEntry> m_resources;

  public:
    Resources() = default;

    bool contains(TypeId type_id) const {
        return m_resources.contains(type_id);
    }

    template<typename T>
    void set(TypeId type_id, T&& val) {
        using U = std::remove_cvref_t<T>;
        emplace<U, U>(type_id, std::forward<T>(val));
    }

    void set(TypeId type_id, Val val) {
        if (!val) {
            fatal("Cannot store an empty Val as a resource");
        }
        if (val.type_id() != type_id) {
            fatal(
                "Resource TypeId {} does not match Val TypeId {}",
                type_id.id(),
                val.type_id().id()
            );
        }

        ResourceEntry entry {
            .value = std::move(val),
            .ref = &make_val_ref,
            .const_ref = &make_val_const_ref,
        };
        m_resources.insert_or_assign(type_id, std::move(entry));
    }

    template<typename Exposed, typename Stored, typename... Args>
    void emplace(TypeId type_id, Args&&... args) {
        ResourceEntry entry {
            .value = make_val<Stored>(std::forward<Args>(args)...),
            .ref = &make_resource_ref<Exposed, Stored>,
            .const_ref = &make_resource_const_ref<Exposed, Stored>,
        };
        m_resources.insert_or_assign(type_id, std::move(entry));
    }

    Ref get(TypeId type_id) {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            return it->second.ref(it->second.value);
        }
        return {};
    }

    Ref get(TypeId type_id) const {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            return it->second.const_ref(it->second.value);
        }
        return {};
    }

    Ref get_mut(TypeId type_id) {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            return it->second.ref(it->second.value);
        }
        return {};
    }
};

} // namespace fei
