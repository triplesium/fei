#pragma once
#include "app/plugin.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/plugin.hpp"

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
    AssetLoadResult<TextAsset>
    load(Reader& reader, const LoadContext& context) override {
        return std::make_unique<TextAsset>(reader.as_string());
    }
};

class TextAssetPlugin : public Plugin {
  public:
    void setup(App& app) override {
        app.add_plugin<AssetPlugin<TextAsset, TextAssetLoader>>();
    }
};

} // namespace fei
