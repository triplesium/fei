#pragma once
#include "app/asset.hpp"
#include "app/plugin.hpp"
#include "graphics/program.hpp"
#include "graphics/texture2d.hpp"

namespace fei {

class Texture2DLoader : public AssetLoader<Texture2D> {
  public:
    virtual Texture2D* load_asset(const std::filesystem::path& path) override;
};

class ProgramLoader : public AssetLoader<Program> {
  public:
    virtual Program* load_asset(const std::filesystem::path& path) override;
};

class GraphicsPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
