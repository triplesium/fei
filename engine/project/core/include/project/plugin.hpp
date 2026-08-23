#pragma once
#include "app/plugin.hpp"
#include "project/project.hpp"

#include <utility>

namespace ets {

class ProjectPlugin : public Plugin {
  private:
    Project m_project;

  public:
    explicit ProjectPlugin(Project project) : m_project(std::move(project)) {}

    void dependencies(PluginDependencies& dependencies) const override;
    void setup(App& app) override;
};

} // namespace ets
