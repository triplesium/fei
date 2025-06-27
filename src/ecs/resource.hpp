#pragma once

#include "refl/val.hpp"

#include <unordered_map>

namespace fei {

class Resources {
  private:
    std::unordered_map<TypeId, Val> m_resources;

  public:
    Resources() = default;

    template<typename T>
    void set(TypeId type_id, T&& val) {
        m_resources.emplace(type_id, make_val<T>(std::forward<T>(val)));
    }

    Ref get(TypeId type_id) const {
        auto it = m_resources.find(type_id);
        if (it != m_resources.end()) {
            return it->second.ref();
        }
        return {};
    }
};
} // namespace fei

// #include "ecs/system.hpp"
// #include "ecs/world.hpp"

// namespace fei {

// template<typename T>
// class Res : public SystemParam {
//     T* m_resource = nullptr;

//   public:
//     void prepare(World& world) override {
//         m_resource = &world.template get_resource<T>();
//     }

//     T& get() { return *m_resource; }
//     const T& get() const { return *m_resource; }
//     T& operator*() { return *m_resource; }
//     const T& operator*() const { return *m_resource; }
//     T* operator->() { return m_resource; }
//     const T* operator->() const { return m_resource; }
// };

// } // namespace fei
