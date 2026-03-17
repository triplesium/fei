#pragma once
#include "asset/loader.hpp"

#include <string>

namespace fei {

class ScriptAsset {
  private:
    std::string m_content;

  public:
    ScriptAsset(std::string content) : m_content(std::move(content)) {}

    const std::string& content() const { return m_content; }
};

class ScriptAssetLoader : public AssetLoader<ScriptAsset> {
  public:
    std::expected<std::unique_ptr<ScriptAsset>, std::error_code>
    load(Reader& reader, const LoadContext& /*context*/) override {
        return std::make_unique<ScriptAsset>(reader.as_string());
    }
};

} // namespace fei
