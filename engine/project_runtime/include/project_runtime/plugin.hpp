#pragma once

#include "app/plugin.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/handle.hpp"
#include "asset/path.hpp"
#include "asset/plugin.hpp"
#include "base/optional.hpp"
#include "core/plugin.hpp"
#include "project/plugin.hpp"
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

FEI_REFLECT(Plugin)
class ProjectRuntimePlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<ProjectPlugin>();
        dependencies.require<ReflectionPlugin>();
        dependencies.require<CorePlugin>();
        dependencies.require<AssetPlugin<SceneDocument, SceneDocumentLoader>>();
    }

    void setup(App& app) override;
};

} // namespace fei
