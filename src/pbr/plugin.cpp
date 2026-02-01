#include "pbr/plugin.hpp"

#include "pbr/forward_render.hpp"
#include "pbr/material.hpp"
#include "rendering/material.hpp"

namespace fei {

void PbrPlugin::setup(App& app) {
    app.add_plugins(
        MaterialPlugin<StandardMaterial> {},
        ForwardRenderPlugin {}
    );
}

} // namespace fei
