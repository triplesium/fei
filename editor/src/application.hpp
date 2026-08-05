#pragma once

#include "app/app.hpp"
#include "editor/layer.hpp"
#include "project/project.hpp"

namespace fei::editor {

class EditorApplication {
  public:
    explicit EditorApplication(Project project);
    EditorApplication(const EditorApplication&) = delete;
    EditorApplication& operator=(const EditorApplication&) = delete;
    EditorApplication(EditorApplication&&) = delete;
    EditorApplication& operator=(EditorApplication&&) = delete;
    ~EditorApplication();

    void run();

  private:
    void shutdown() noexcept;
    void update_frame();

    App m_app;
    EditorLayer m_editor_layer;
};

} // namespace fei::editor
