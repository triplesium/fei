#pragma once

#include "app/plugin.hpp"
#include "ecs/commands.hpp"
#include "ecs/hierarchy.hpp"
#include "ecs/query.hpp"
#include "ecs/system_set.hpp"
#include "input/input.hpp"
#include "input_focus/focus.hpp"
#include "input_focus/plugin.hpp"
#include "input_focus/tab_navigation.hpp"
#include "text/pipeline.hpp"
#include "text/text.hpp"
#include "ui/image.hpp"
#include "ui/interaction.hpp"
#include "ui/measurement.hpp"
#include "ui/node.hpp"
#include "ui/surface.hpp"
#include "ui/text.hpp"

namespace fei {

class App;
struct Window;

namespace ui {

struct Systems {
    struct Focus : SystemSet<Focus> {};
    struct Prepare : SystemSet<Prepare> {};
    struct Invalidate : SystemSet<Invalidate> {};
    struct Content : SystemSet<Content> {};
    struct Layout : SystemSet<Layout> {};
    struct TextInvalidate : SystemSet<TextInvalidate> {};
    struct TextLayout : SystemSet<TextLayout> {};
    struct PostLayout : SystemSet<PostLayout> {};
    struct Stack : SystemSet<Stack> {};
};

void update_interactions(
    Query<Entity, const Node, const ComputedNode> nodes,
    Query<Entity, Interaction> interactions,
    Query<Entity, RelativeCursorPosition> cursor_positions,
    Query<Entity, const FocusPolicy> focus_policies,
    Query<Entity, const InteractionDisabled> disabled,
    Query<Entity, const input_focus::TabIndex> tab_indices,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const CalculatedClip> calculated_clips,
    ResRO<Stack> stack,
    ResRO<MouseInput> mouse,
    ResRO<Window> window,
    ResRW<input_focus::InputFocus> input_focus,
    ResRW<input_focus::InputFocusVisible> input_focus_visible
);

void sync_computed_nodes(
    Query<Entity, const Node>::Filter<Without<ComputedNode>> nodes,
    Query<Entity, const Node>::Filter<Without<ScrollPosition>>
        missing_scroll_positions,
    Commands commands
);

void sync_computed_stack_indices(
    Query<Entity, const Node>::Filter<Without<ComputedStackIndex>> nodes,
    Commands commands
);

void sync_image_nodes(
    Query<Entity, const ImageNode>::Filter<Without<Node>> missing_nodes,
    Query<Entity, const ImageNode>::Filter<Without<ImageNodeSize>>
        missing_image_sizes,
    Query<Entity, const ImageNode>::Filter<Without<ContentSize>>
        missing_content_sizes,
    Commands commands
);

void update_image_content_sizes(
    Query<Entity, const ImageNode, ImageNodeSize, ContentSize> images,
    ResRO<Assets<Image>> image_assets
);

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
);

void invalidate_text_nodes(
    Query<Entity>::Filter<
        Or<Changed<Text>, Changed<text::TextFont>, Changed<text::TextLayout>>>
        changed_texts,
    Query<Entity, TextNodeFlags> flags,
    ResRO<Assets<text::Font>> fonts
);

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
);

void invalidate_text_layouts(
    Query<Entity, const ComputedNode>::Filter<Changed<ComputedNode>, With<Text>>
        changed_nodes,
    Query<Entity, TextNodeFlags> flags
);

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
);

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
);

void update_clipping(
    Query<Entity, const Node, const ComputedNode> nodes,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Query<Entity, CalculatedClip> calculated_clips,
    Commands commands
);

void compute_stack(
    Query<Entity, const Node, ComputedStackIndex> nodes,
    Query<Entity, const Children> child_lists,
    Query<Entity, const ChildOf> parents,
    Query<Entity, const ZIndex> z_indices,
    ResRW<Stack> stack
);

FEI_REFLECT(Plugin)
class UiPlugin : public fei::Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ui
} // namespace fei
