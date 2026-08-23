#include "text/plugin.hpp"

#include "app/app.hpp"
#include "asset/plugin.hpp"
#include "text/editable.hpp"
#include "text/font.hpp"
#include "text/pipeline.hpp"

namespace ets::text {

void TextPlugin::dependencies(PluginDependencies& dependencies) const {
    dependencies.require<AssetPlugin<Font, FontLoader>>();
}

void TextPlugin::setup(App& app) {
    app.add_resource(TextPipeline {})
        .add_event<TextChanged>()
        .add_systems(
            PreUpdate,
            apply_text_edits | in_set<EditableTextSystems::Apply>()
        );
}

} // namespace ets::text
