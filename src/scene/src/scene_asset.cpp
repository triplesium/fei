#include "base/result.hpp"
#include "scene/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fei {

namespace {

SceneValidationError validation_error(
    SceneValidationError::Kind kind,
    SceneNodeId node,
    Optional<SceneNodeId> related_node,
    std::string message
) {
    return SceneValidationError {
        .kind = kind,
        .node = node,
        .related_node = std::move(related_node),
        .message = std::move(message),
    };
}

} // namespace

Status<SceneMesh::ValidationError> SceneMesh::validate() const {
    for (std::size_t index = 0; index < primitives.size(); ++index) {
        const auto& primitive = primitives[index];
        if (!primitive.mesh) {
            return failure(
                ValidationError {
                    .kind = ValidationError::Kind::InvalidMesh,
                    .primitive_index = index,
                    .message = "SceneMesh primitive " + std::to_string(index) +
                               " has an invalid mesh handle",
                }
            );
        }
        if (!primitive.material) {
            return failure(
                ValidationError {
                    .kind = ValidationError::Kind::InvalidMaterial,
                    .primitive_index = index,
                    .message = "SceneMesh primitive " + std::to_string(index) +
                               " has an invalid material handle",
                }
            );
        }
    }
    return {};
}

Optional<const SceneNode&> Scene::node(SceneNodeId id) const {
    const auto index = static_cast<std::size_t>(id);
    if (index >= nodes.size()) {
        return nullopt;
    }
    return nodes[index];
}

Status<SceneValidationError> Scene::validate() const {
    if (nodes.size() > static_cast<std::size_t>(invalid_scene_node_id)) {
        return failure(validation_error(
            SceneValidationError::Kind::TooManyNodes,
            invalid_scene_node_id,
            nullopt,
            "Scene contains more nodes than SceneNodeId can address"
        ));
    }

    std::vector<std::uint8_t> is_root(nodes.size(), 0);
    for (auto root : roots) {
        const auto root_index = static_cast<std::size_t>(root);
        if (root_index >= nodes.size()) {
            return failure(validation_error(
                SceneValidationError::Kind::InvalidRoot,
                root,
                nullopt,
                "Scene root " + std::to_string(root) + " is out of range"
            ));
        }
        if (is_root[root_index] != 0) {
            return failure(validation_error(
                SceneValidationError::Kind::DuplicateRoot,
                root,
                nullopt,
                "Scene root " + std::to_string(root) + " is duplicated"
            ));
        }
        is_root[root_index] = 1;
    }

    std::vector<SceneNodeId> parents(nodes.size(), invalid_scene_node_id);
    for (std::size_t parent_index = 0; parent_index < nodes.size();
         ++parent_index) {
        const auto parent = static_cast<SceneNodeId>(parent_index);
        for (auto child : nodes[parent_index].children) {
            const auto child_index = static_cast<std::size_t>(child);
            if (child_index >= nodes.size()) {
                return failure(validation_error(
                    SceneValidationError::Kind::InvalidChild,
                    parent,
                    child,
                    "Scene node " + std::to_string(parent) +
                        " references out-of-range child " +
                        std::to_string(child)
                ));
            }

            auto existing_parent = parents[child_index];
            if (existing_parent == parent) {
                return failure(validation_error(
                    SceneValidationError::Kind::DuplicateChild,
                    parent,
                    child,
                    "Scene node " + std::to_string(parent) +
                        " references child " + std::to_string(child) +
                        " more than once"
                ));
            }
            if (existing_parent != invalid_scene_node_id) {
                return failure(validation_error(
                    SceneValidationError::Kind::MultipleParents,
                    child,
                    parent,
                    "Scene node " + std::to_string(child) +
                        " has multiple parents " +
                        std::to_string(existing_parent) + " and " +
                        std::to_string(parent)
                ));
            }
            parents[child_index] = parent;
        }
    }

    for (auto root : roots) {
        auto parent = parents[static_cast<std::size_t>(root)];
        if (parent != invalid_scene_node_id) {
            return failure(validation_error(
                SceneValidationError::Kind::RootHasParent,
                root,
                parent,
                "Scene root " + std::to_string(root) + " has parent " +
                    std::to_string(parent)
            ));
        }
    }

    enum class VisitState : std::uint8_t {
        Unvisited,
        Visiting,
        Visited,
    };
    struct VisitFrame {
        SceneNodeId node;
        std::size_t next_child;
    };

    std::vector<VisitState> visit_states(nodes.size(), VisitState::Unvisited);
    std::vector<VisitFrame> visit_stack;
    for (std::size_t start_index = 0; start_index < nodes.size();
         ++start_index) {
        if (visit_states[start_index] != VisitState::Unvisited) {
            continue;
        }

        const auto start = static_cast<SceneNodeId>(start_index);
        visit_states[start_index] = VisitState::Visiting;
        visit_stack.push_back({.node = start, .next_child = 0});
        while (!visit_stack.empty()) {
            auto& frame = visit_stack.back();
            const auto frame_index = static_cast<std::size_t>(frame.node);
            const auto& children = nodes[frame_index].children;
            if (frame.next_child >= children.size()) {
                visit_states[frame_index] = VisitState::Visited;
                visit_stack.pop_back();
                continue;
            }

            const auto child = children[frame.next_child++];
            const auto child_index = static_cast<std::size_t>(child);
            if (visit_states[child_index] == VisitState::Visiting) {
                return failure(validation_error(
                    SceneValidationError::Kind::Cycle,
                    frame.node,
                    child,
                    "Scene hierarchy contains a cycle from node " +
                        std::to_string(frame.node) + " to node " +
                        std::to_string(child)
                ));
            }
            if (visit_states[child_index] == VisitState::Unvisited) {
                visit_states[child_index] = VisitState::Visiting;
                visit_stack.push_back({.node = child, .next_child = 0});
            }
        }
    }

    std::vector<std::uint8_t> reachable(nodes.size(), 0);
    std::vector<SceneNodeId> reachable_stack(roots.begin(), roots.end());
    while (!reachable_stack.empty()) {
        const auto current = reachable_stack.back();
        reachable_stack.pop_back();
        const auto current_index = static_cast<std::size_t>(current);
        if (reachable[current_index] != 0) {
            continue;
        }
        reachable[current_index] = 1;
        const auto& children = nodes[current_index].children;
        reachable_stack
            .insert(reachable_stack.end(), children.begin(), children.end());
    }

    for (std::size_t index = 0; index < reachable.size(); ++index) {
        if (reachable[index] == 0) {
            const auto node_id = static_cast<SceneNodeId>(index);
            return failure(validation_error(
                SceneValidationError::Kind::UnreachableNode,
                node_id,
                nullopt,
                "Scene node " + std::to_string(node_id) +
                    " is not reachable from a root"
            ));
        }
    }

    return {};
}

Optional<Entity> SceneInstance::node_entity(SceneNodeId node) const {
    const auto index = static_cast<std::size_t>(node);
    if (index >= node_entities.size()) {
        return nullopt;
    }
    return node_entities[index];
}

} // namespace fei
