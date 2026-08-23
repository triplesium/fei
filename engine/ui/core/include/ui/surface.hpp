#pragma once

#include "ecs/fwd.hpp"
#include "math/vector.hpp"
#include "ui/measurement.hpp"
#include "ui/node.hpp"

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

namespace ets::ui {

class Surface {
  public:
    void clear();
    void upsert(
        Entity entity,
        const Node& node,
        ContentSize content_size = {},
        BorderRadius border_radius = {},
        ScrollPosition scroll_position = {}
    );
    void upsert(
        Entity entity,
        const Node& node,
        Vector2 content_size,
        BorderRadius border_radius = {},
        ScrollPosition scroll_position = {}
    );
    void set_children(Entity entity, std::span<const Entity> children);
    void remove(Entity entity);

    void compute(Entity root, Vector2 viewport_size);

    [[nodiscard]] bool contains(Entity entity) const;
    [[nodiscard]] const ComputedNode* get(Entity entity) const;

  private:
    struct Entry {
        Node node;
        ContentSize content_size;
        BorderRadius border_radius;
        ScrollPosition scroll_position;
        ComputedNode computed;
        std::vector<Entity> children;
    };

    void layout_node(Entity entity, Vector2 position, Vector2 size);
    void translate_subtree(Entity entity, Vector2 offset);

    std::unordered_map<Entity, Entry> m_entries;
};

struct LayoutState {
    Vector2 viewport_size;
    uint64 generation {0};
    std::size_t node_count {0};
    std::size_t content_size_count {0};
    std::size_t border_radius_count {0};
    std::size_t children_count {0};
    std::size_t parent_count {0};
    bool initialized {false};
};

} // namespace ets::ui
