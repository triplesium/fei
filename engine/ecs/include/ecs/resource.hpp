#pragma once

#include "base/log.hpp"
#include "ecs/change_detection.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

class Resources {
  private:
    struct ResourceEntry {
        Val value;
        Ref external;
        Ref (*ref)(Val&) {nullptr};
        Ref (*const_ref)(const Val&) {nullptr};
        ComponentTicks ticks;
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
    std::vector<TypeId> m_insertion_order;

  public:
    Resources() = default;
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    Resources(Resources&&) noexcept = default;
    Resources& operator=(Resources&& other) noexcept {
        if (this != &other) {
            clear();
            m_resources = std::move(other.m_resources);
            m_insertion_order = std::move(other.m_insertion_order);
        }
        return *this;
    }
    ~Resources() { clear(); }

    bool contains(TypeId type_id) const {
        return m_resources.contains(type_id);
    }

    template<typename T>
    void set(TypeId type_id, Tick tick, T&& val) {
        using U = std::remove_cvref_t<T>;
        emplace<U, U>(type_id, tick, std::forward<T>(val));
    }

    void set(TypeId type_id, Tick tick, Val val) {
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

        const bool inserted = !m_resources.contains(type_id);
        ResourceEntry entry {
            .value = std::move(val),
            .external = {},
            .ref = &make_val_ref,
            .const_ref = &make_val_const_ref,
            .ticks = ComponentTicks::added_at(tick),
        };
        if (auto it = m_resources.find(type_id); it != m_resources.end()) {
            entry.ticks.added = it->second.ticks.added;
        }
        m_resources.insert_or_assign(type_id, std::move(entry));
        if (inserted) {
            m_insertion_order.push_back(type_id);
        }
    }

    template<typename Exposed, typename Stored, typename... Args>
    void emplace(TypeId type_id, Tick tick, Args&&... args) {
        const bool inserted = !m_resources.contains(type_id);
        ResourceEntry entry {
            .value = make_val<Stored>(std::forward<Args>(args)...),
            .external = {},
            .ref = &make_resource_ref<Exposed, Stored>,
            .const_ref = &make_resource_const_ref<Exposed, Stored>,
            .ticks = ComponentTicks::added_at(tick),
        };
        if (auto it = m_resources.find(type_id); it != m_resources.end()) {
            entry.ticks.added = it->second.ticks.added;
        }
        m_resources.insert_or_assign(type_id, std::move(entry));
        if (inserted) {
            m_insertion_order.push_back(type_id);
        }
    }

    template<typename T>
    void set_readonly_ref(TypeId type_id, Tick tick, const T& resource) {
        const bool inserted = !m_resources.contains(type_id);
        ResourceEntry entry {
            .value = {},
            .external = Ref(resource),
            .ref = nullptr,
            .const_ref = nullptr,
            .ticks = ComponentTicks::added_at(tick),
        };
        if (auto it = m_resources.find(type_id); it != m_resources.end()) {
            entry.ticks.added = it->second.ticks.added;
        }
        m_resources.insert_or_assign(type_id, std::move(entry));
        if (inserted) {
            m_insertion_order.push_back(type_id);
        }
    }

    void clear() noexcept {
        for (auto type = m_insertion_order.rbegin();
             type != m_insertion_order.rend();
             ++type) {
            m_resources.erase(*type);
        }
        m_resources.clear();
        m_insertion_order.clear();
    }

    Ref get(TypeId type_id) {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            if (it->second.external) {
                return it->second.external;
            }
            return it->second.ref(it->second.value);
        }
        return {};
    }

    Ref get(TypeId type_id) const {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            if (it->second.external) {
                return it->second.external;
            }
            return it->second.const_ref(it->second.value);
        }
        return {};
    }

    Ref get_mut(TypeId type_id) {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            if (it->second.external) {
                return it->second.external;
            }
            return it->second.ref(it->second.value);
        }
        return {};
    }

    ComponentTicks& ticks(TypeId type_id) {
        auto it = m_resources.find(type_id);
        if (it == m_resources.end()) {
            fatal("Resource with type id {} not found", type_id.id());
        }
        return it->second.ticks;
    }

    const ComponentTicks& ticks(TypeId type_id) const {
        auto it = m_resources.find(type_id);
        if (it == m_resources.end()) {
            fatal("Resource with type id {} not found", type_id.id());
        }
        return it->second.ticks;
    }
};

} // namespace fei
