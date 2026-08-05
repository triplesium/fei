#pragma once

#include <memory>

namespace fei {
class World;
}

namespace fei::editor {

class EditorLayer {
  public:
    EditorLayer();
    EditorLayer(const EditorLayer&) = delete;
    EditorLayer& operator=(const EditorLayer&) = delete;
    EditorLayer(EditorLayer&&) = delete;
    EditorLayer& operator=(EditorLayer&&) = delete;
    ~EditorLayer();

    void draw(World& world);
    void shutdown(World& world) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fei::editor
