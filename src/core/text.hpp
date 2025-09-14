#pragma once
#include "app/plugin.hpp"
#include "asset/asset_loader.hpp"
#include "asset/asset_server.hpp"

#include <fstream>
#include <string>

namespace fei {

class TextAsset {
  private:
    std::string m_text;

  public:
    TextAsset(const std::string& text) : m_text(text) {}
    const std::string& text() const { return m_text; }
    void set_text(const std::string& text) { m_text = text; }
};

class TextAssetLoader : public AssetLoader<TextAsset> {
  protected:
    virtual TextAsset* load(const std::filesystem::path& path) override {
        std::ifstream file(path);
        if (!file) {
            return nullptr;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return new TextAsset(buffer.str());
    }
};

class TextAssetPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.resource<AssetServer>().add_loader<TextAsset, TextAssetLoader>();
    }
};

} // namespace fei
