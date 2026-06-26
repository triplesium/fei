#pragma once
#include "asset/loader.hpp"

#include <string>
#include <utility>

namespace fei {

class ScriptAsset {
  private:
    std::string m_content;

  public:
    ScriptAsset(std::string content) : m_content(std::move(content)) {}

    const std::string& content() const { return m_content; }
    void set_content(std::string content) { m_content = std::move(content); }
};

class ScriptAssetLoader : public AssetLoader<ScriptAsset> {
  public:
    AssetLoadResult<ScriptAsset>
    load(Reader& reader, const LoadContext& /*context*/) override {
        return std::make_unique<ScriptAsset>(reader.as_string());
    }
};

} // namespace fei
