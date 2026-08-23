#include "ui/plugin.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "ecs/system_config.hpp"
#include "text/font.hpp"
#include "text/pipeline.hpp"
#include "text/plugin.hpp"
#include "text/text.hpp"
#include "window/window.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets::ui {

namespace {

void require_text_measure(ComponentRW<TextNodeFlags> flags) {
    auto next = flags.read();
    next.needs_measure = true;
    next.needs_layout = true;
    if (flags.read() != next) {
        flags = next;
    }
}

} // namespace

void sync_computed_nodes(
    Query<Entity, const Node>::Filter<Without<ComputedNode>> nodes,
    Query<Entity, const Node>::Filter<Without<ScrollPosition>>
        missing_scroll_positions,
    Commands commands
) {
    for (const auto& [entity, node] : nodes) {
        (void)node;
        commands.entity(entity).add(ComputedNode {});
    }
    for (const auto& [entity, node] : missing_scroll_positions) {
        (void)node;
        commands.entity(entity).add(ScrollPosition {});
    }
}

void sync_computed_stack_indices(
    Query<Entity, const Node>::Filter<Without<ComputedStackIndex>> nodes,
    Commands commands
) {
    for (const auto& [entity, node] : nodes) {
        (void)node;
        commands.entity(entity).add(ComputedStackIndex {});
    }
}

void sync_image_nodes(
    Query<Entity, const ImageNode>::Filter<Without<Node>> missing_nodes,
    Query<Entity, const ImageNode>::Filter<Without<ImageNodeSize>>
        missing_image_sizes,
    Query<Entity, const ImageNode>::Filter<Without<ContentSize>>
        missing_content_sizes,
    Commands commands
) {
    for (const auto& [entity, image] : missing_nodes) {
        (void)image;
        commands.entity(entity).add(Node {});
    }
    for (const auto& [entity, image] : missing_image_sizes) {
        (void)image;
        commands.entity(entity).add(ImageNodeSize {});
    }
    for (const auto& [entity, image] : missing_content_sizes) {
        (void)image;
        commands.entity(entity).add(ContentSize {});
    }
}

void update_image_content_sizes(
    Query<Entity, const ImageNode, ImageNodeSize, ContentSize> images,
    ResRO<Assets<Image>> image_assets
) {
    for (auto [entity, image_node, image_size, content_size] : images) {
        (void)entity;
        Vector2 size;
        if (const auto image = image_assets->get(image_node.image)) {
            size = {
                static_cast<float>(image->width()),
                static_cast<float>(image->height()),
            };
            if (image_node.source_rect) {
                size =
                    image_node.source_rect->max - image_node.source_rect->min;
            }
        }
        const ImageNodeSize new_image_size {.size = size};
        if (image_size.read() != new_image_size) {
            image_size = new_image_size;
        }
        const ContentSize new_content_size {
            .measure = FixedMeasure {
                .size = image_node.mode == NodeImageMode::Auto ? size :
                                                                 Vector2::Zero,
            },
        };
        if (content_size.read() != new_content_size) {
            content_size = new_content_size;
        }
    }
}

void sync_text_nodes(
    Query<Entity, const Text>::Filter<Without<Node>> missing_nodes,
    Query<Entity, const Text>::Filter<Without<ContentSize>>
        missing_content_sizes,
    Query<Entity, const Text>::Filter<Without<text::TextFont>> missing_fonts,
    Query<Entity, const Text>::Filter<Without<text::TextColor>> missing_colors,
    Query<Entity, const Text>::Filter<Without<text::TextLayout>>
        missing_text_layouts,
    Query<Entity, const Text>::Filter<Without<text::TextLayoutInfo>>
        missing_layouts,
    Query<Entity, const Text>::Filter<Without<TextNodeFlags>> missing_flags,
    Commands commands
) {
    for (const auto& [entity, value] : missing_nodes) {
        (void)value;
        commands.entity(entity).add(Node {});
    }
    for (const auto& [entity, value] : missing_content_sizes) {
        (void)value;
        commands.entity(entity).add(ContentSize {});
    }
    for (const auto& [entity, value] : missing_fonts) {
        (void)value;
        commands.entity(entity).add(text::TextFont {});
    }
    for (const auto& [entity, value] : missing_colors) {
        (void)value;
        commands.entity(entity).add(text::TextColor {});
    }
    for (const auto& [entity, value] : missing_text_layouts) {
        (void)value;
        commands.entity(entity).add(text::TextLayout {});
    }
    for (const auto& [entity, value] : missing_layouts) {
        (void)value;
        commands.entity(entity).add(text::TextLayoutInfo {});
    }
    for (const auto& [entity, value] : missing_flags) {
        (void)value;
        commands.entity(entity).add(TextNodeFlags {});
    }
}

void invalidate_text_nodes(
    Query<Entity>::Filter<
        Or<Changed<Text>, Changed<text::TextFont>, Changed<text::TextLayout>>>
        changed_texts,
    Query<Entity, TextNodeFlags> flags,
    ResRO<Assets<text::Font>> fonts
) {
    if (fonts.is_changed()) {
        for (auto [entity, node_flags] : flags) {
            (void)entity;
            require_text_measure(node_flags);
        }
        return;
    }

    for (const auto& [entity] : changed_texts) {
        if (auto item = flags.get(entity)) {
            require_text_measure(std::get<1>(*item));
        }
    }
}

void update_text_content_sizes(
    Query<
        Entity,
        const Text,
        const text::TextFont,
        const text::TextLayout,
        ContentSize,
        TextNodeFlags> texts,
    ResRO<Assets<text::Font>> fonts,
    ResRO<text::TextPipeline> pipeline
) {
    for (auto [entity, value, text_font, text_layout, content_size, flags] :
         texts) {
        (void)entity;
        if (!flags.read().needs_measure) {
            continue;
        }
        auto measure = TextMeasure {};
        measure.layout = text_layout;
        if (const auto font = fonts->get(text_font.font)) {
            measure.info = pipeline->create_measure(
                *font,
                value.value,
                text_font.font_size
            );
        }
        const ContentSize next {.measure = std::move(measure)};
        if (content_size.read() != next) {
            content_size = next;
        }
        auto next_flags = flags.read();
        next_flags.needs_measure = false;
        next_flags.needs_layout = true;
        if (flags.read() != next_flags) {
            flags = next_flags;
        }
    }
}

void invalidate_text_layouts(
    Query<Entity, const ComputedNode>::Filter<Changed<ComputedNode>, With<Text>>
        changed_nodes,
    Query<Entity, TextNodeFlags> flags
) {
    for (const auto& [entity, computed] : changed_nodes) {
        if (auto item = flags.get(entity)) {
            auto node_flags = std::get<1>(*item);
            auto next = node_flags.read();
            if (!next.has_layout_size ||
                next.layout_size != computed.content_size) {
                next.has_layout_size = true;
                next.layout_size = computed.content_size;
                next.needs_layout = true;
                if (node_flags.read() != next) {
                    node_flags = next;
                }
            }
        }
    }
}

void update_text_layouts(
    Query<
        Entity,
        const Text,
        const text::TextFont,
        const text::TextLayout,
        const ContentSize,
        const ComputedNode,
        text::TextLayoutInfo,
        TextNodeFlags> texts,
    ResRO<Assets<text::Font>> fonts,
    ResRW<Assets<Image>> images,
    ResRW<text::TextPipeline> pipeline
) {
    for (auto
         [entity,
          value,
          text_font,
          text_layout,
          content_size,
          computed,
          layout,
          flags] : texts) {
        (void)entity;
        (void)value;
        if (!flags.read().needs_layout) {
            continue;
        }
        auto next = text::TextLayoutInfo {};
        const auto* measure = std::get_if<TextMeasure>(&content_size.measure);
        if (const auto font = fonts->get(text_font.font); font && measure) {
            pipeline->layout(
                text_font.font.id(),
                *font,
                measure->info,
                text_font.font_size,
                text_layout,
                computed.content_size,
                *images,
                next
            );
        }
        if (layout.read() != next) {
            layout = std::move(next);
        }
        auto next_flags = flags.read();
        next_flags.needs_layout = false;
        if (flags.read() != next_flags) {
            flags = next_flags;
        }
    }
}

void compute_layout(
    Query<Entity, const Node, ComputedNode, ScrollPosition> nodes,
    Query<Entity, const ContentSize> content_sizes,
    Query<Entity, const BorderRadius> border_radii,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Query<Entity>::Filter<
        Or<Changed<Node>,
           Changed<ContentSize>,
           Changed<BorderRadius>,
           Changed<ScrollPosition>,
           Changed<Children>,
           Changed<ChildOf>>> changed_inputs,
    ResRO<Window> window,
    ResRW<Surface> surface,
    ResRW<LayoutState> state
) {
    const Vector2 viewport_size {
        static_cast<float>(std::max(window->width, 0)),
        static_cast<float>(std::max(window->height, 0)),
    };
    const auto& previous = static_cast<const ResRW<LayoutState>&>(state).get();
    const auto next_state = LayoutState {
        .viewport_size = viewport_size,
        .generation = previous.generation + 1,
        .node_count = nodes.size(),
        .content_size_count = content_sizes.size(),
        .border_radius_count = border_radii.size(),
        .children_count = child_lists.size(),
        .parent_count = parents.size(),
        .initialized = true,
    };
    if (previous.initialized && previous.viewport_size == viewport_size &&
        previous.node_count == next_state.node_count &&
        previous.content_size_count == next_state.content_size_count &&
        previous.border_radius_count == next_state.border_radius_count &&
        previous.children_count == next_state.children_count &&
        previous.parent_count == next_state.parent_count &&
        changed_inputs.empty()) {
        return;
    }

    surface->clear();
    for (const auto& [entity, node, computed, scroll_position] : nodes) {
        (void)computed;
        const auto content = content_sizes.get(entity);
        const auto radius = border_radii.get(entity);
        surface->upsert(
            entity,
            node,
            content ? std::get<1>(*content) : ContentSize {},
            radius ? std::get<1>(*radius) : BorderRadius {},
            scroll_position.read()
        );
    }

    for (const auto& [entity, node, computed, scroll_position] : nodes) {
        (void)node;
        (void)computed;
        (void)scroll_position;
        if (const auto children = child_lists.get(entity)) {
            surface->set_children(entity, std::get<1>(*children).entities());
        }
    }

    for (const auto& [entity, node, computed, scroll_position] : nodes) {
        (void)node;
        (void)computed;
        (void)scroll_position;
        const auto parent = parents.get(entity);
        if (parent && surface->contains(std::get<1>(*parent).parent)) {
            continue;
        }
        surface->compute(entity, viewport_size);
    }

    for (auto [entity, node, computed, scroll_position] : nodes) {
        (void)node;
        if (const auto* layout = surface->get(entity)) {
            if (computed.read() != *layout) {
                computed = *layout;
            }
            if (scroll_position.read().offset != layout->scroll_position) {
                scroll_position = ScrollPosition {
                    .offset = layout->scroll_position,
                };
            }
        }
    }
    state.get() = next_state;
}

void update_clipping(
    Query<Entity, const Node, const ComputedNode> nodes,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Query<Entity, CalculatedClip> calculated_clips,
    Commands commands
) {
    using Clip = std::optional<Rect>;
    const auto intersect = [](const Rect& lhs, const Rect& rhs) {
        return Rect {
            .min =
                {std::max(lhs.min.x, rhs.min.x),
                 std::max(lhs.min.y, rhs.min.y)},
            .max = {
                std::min(lhs.max.x, rhs.max.x),
                std::min(lhs.max.y, rhs.max.y)
            },
        };
    };
    const auto update_component = [&](Entity entity, const Clip& inherited) {
        const auto existing = calculated_clips.get(entity);
        if (!inherited) {
            if (existing) {
                commands.entity(entity).remove<CalculatedClip>();
            }
            return;
        }
        const CalculatedClip value {.clip = *inherited};
        if (existing) {
            auto component = std::get<1>(*existing);
            if (component.read() != value) {
                component = value;
            }
        } else {
            commands.entity(entity).add(value);
        }
    };

    std::function<void(Entity, Clip)> visit = [&](Entity entity,
                                                  Clip inherited) {
        const auto item = nodes.get(entity);
        if (!item) {
            return;
        }
        const auto& node = std::get<1>(*item);
        const auto& computed = std::get<2>(*item);
        update_component(entity, inherited);

        Clip child_clip = inherited;
        if (node.overflow.x != OverflowAxis::Visible ||
            node.overflow.y != OverflowAxis::Visible) {
            constexpr auto infinity = std::numeric_limits<float>::infinity();
            Rect own_clip {
                .min = computed.position +
                       Vector2 {computed.border.left, computed.border.top},
                .max = computed.position + computed.size -
                       Vector2 {computed.border.right, computed.border.bottom},
            };
            if (node.overflow.x == OverflowAxis::Visible) {
                own_clip.min.x = -infinity;
                own_clip.max.x = infinity;
            }
            if (node.overflow.y == OverflowAxis::Visible) {
                own_clip.min.y = -infinity;
                own_clip.max.y = infinity;
            }
            child_clip =
                child_clip ? intersect(*child_clip, own_clip) : Clip {own_clip};
        }

        if (const auto children = child_lists.get(entity)) {
            for (const auto child : std::get<1>(*children)) {
                visit(child, child_clip);
            }
        }
    };

    for (const auto& [entity, node, computed] : nodes) {
        (void)node;
        (void)computed;
        const auto parent = parents.get(entity);
        if (!parent || !nodes.get(std::get<1>(*parent).parent)) {
            visit(entity, std::nullopt);
        }
    }
}

void compute_stack(
    Query<Entity, const Node, ComputedStackIndex> nodes,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const ZIndex> z_indices,
    ResRW<Stack> stack
) {
    const auto z_index = [&](Entity entity) {
        const auto item = z_indices.get(entity);
        return item ? std::get<1>(*item).value : 0;
    };
    std::vector<Entity> roots;
    roots.reserve(nodes.size());
    for (const auto& [entity, node, computed] : nodes) {
        (void)computed;
        if (node.display == Display::None) {
            continue;
        }
        const auto parent = parents.get(entity);
        if (!parent || !nodes.get(std::get<1>(*parent).parent)) {
            roots.push_back(entity);
        }
    }
    std::stable_sort(roots.begin(), roots.end(), [&](Entity lhs, Entity rhs) {
        const auto lhs_key = std::pair {z_index(lhs), lhs};
        const auto rhs_key = std::pair {z_index(rhs), rhs};
        return lhs_key < rhs_key;
    });

    stack->nodes.clear();
    stack->nodes.reserve(nodes.size());
    std::function<void(Entity)> append_subtree = [&](Entity entity) {
        stack->nodes.push_back(entity);
        const auto children_item = child_lists.get(entity);
        if (!children_item) {
            return;
        }

        std::vector<Entity> children;
        for (const auto child : std::get<1>(*children_item)) {
            const auto node_item = nodes.get(child);
            if (node_item && std::get<1>(*node_item).display != Display::None) {
                children.push_back(child);
            }
        }
        std::stable_sort(
            children.begin(),
            children.end(),
            [&](Entity lhs, Entity rhs) {
                return z_index(lhs) < z_index(rhs);
            }
        );
        for (const auto child : children) {
            append_subtree(child);
        }
    };
    for (const auto root : roots) {
        append_subtree(root);
    }

    std::unordered_map<Entity, uint32> indices;
    indices.reserve(stack->nodes.size());
    for (std::size_t index = 0; index < stack->nodes.size(); ++index) {
        indices.emplace(stack->nodes[index], static_cast<uint32>(index));
    }
    for (auto [entity, node, computed] : nodes) {
        (void)node;
        const auto index = indices.find(entity);
        const ComputedStackIndex value {
            .value = index == indices.end() ? ComputedStackIndex::HIDDEN :
                                              index->second,
        };
        if (computed.read() != value) {
            computed = value;
        }
    }
}

void UiPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<ImagePlugin>()
        .require<text::TextPlugin>()
        .require<input_focus::InputFocusPlugin>();
}

void UiPlugin::setup(App& app) {
    if (!app.has_resource<Window>()) {
        fatal("ui::UiPlugin requires Window to be installed first");
    }

    app.configure_sets(
           PreUpdate,
           chain(
               InputSystems::ApplyDevtools {},
               input_focus::Systems::Navigation {},
               Systems::Focus {}
           )
    )
        .add_systems(PreUpdate, update_interactions | in_set<Systems::Focus>())
        .add_resource(Surface {})
        .add_resource(LayoutState {})
        .add_resource(Stack {})
        .configure_sets(
            PostUpdate,
            chain(
                Systems::Prepare {},
                Systems::Invalidate {},
                Systems::Content {},
                Systems::Layout {},
                Systems::TextInvalidate {},
                Systems::TextLayout {},
                Systems::PostLayout {},
                Systems::Stack {}
            )
        )
        .add_systems(
            PostUpdate,
            sync_computed_nodes | in_set<Systems::Prepare>(),
            sync_computed_stack_indices | in_set<Systems::Prepare>(),
            sync_image_nodes | in_set<Systems::Prepare>(),
            sync_text_nodes | in_set<Systems::Prepare>(),
            invalidate_text_nodes | in_set<Systems::Invalidate>(),
            update_image_content_sizes | in_set<Systems::Content>(),
            update_text_content_sizes | in_set<Systems::Content>(),
            compute_layout | in_set<Systems::Layout>(),
            invalidate_text_layouts | in_set<Systems::TextInvalidate>(),
            update_text_layouts | in_set<Systems::TextLayout>(),
            update_clipping | in_set<Systems::PostLayout>(),
            compute_stack | in_set<Systems::Stack>()
        );
}

} // namespace ets::ui
