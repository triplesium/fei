#pragma once
#include "app/plugin.hpp"
#include "asset/loader.hpp"
#include "asset/server.hpp"

#include <expected>
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
    virtual std::expected<std::unique_ptr<TextAsset>, std::error_code>
    load(const std::filesystem::path& path) override {
        std::ifstream file(path);
        if (!file) {
            return std::unexpected(std::error_code {});
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return std::make_unique<TextAsset>(buffer.str());
    }
};

class TextAssetPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.resource<AssetServer>().add_loader<TextAsset, TextAssetLoader>();
    }
};

} // namespace fei
