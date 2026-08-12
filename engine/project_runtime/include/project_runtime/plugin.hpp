#pragma once

#include "app/plugin.hpp"
#include "asset/handle.hpp"
#include "asset/path.hpp"
#include "base/optional.hpp"
#include "scene/document.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fei {

enum class ProjectRuntimeSceneStatus : std::uint8_t {
    NotConfigured,
    Loaded,
    Failed,
};

struct ProjectRuntimeState {
    ProjectRuntimeSceneStatus scene_status {
        ProjectRuntimeSceneStatus::NotConfigured
    };
    Optional<AssetPath> scene_path;
    Optional<Handle<SceneDocument>> scene_asset;
    SceneEntityBindings scene_entities;
    std::vector<std::string> warnings;
    std::string error;
};

class ProjectRuntimePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
