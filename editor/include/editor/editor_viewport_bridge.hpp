#pragma once

namespace fei {

class App;

namespace editor {

void install_editor_viewport_bridge(App& app);
void cleanup_editor_viewport_bridge(App& app) noexcept;

} // namespace editor
} // namespace fei
