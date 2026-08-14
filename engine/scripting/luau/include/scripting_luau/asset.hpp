#pragma once

#include "asset/loader.hpp"

#include <string>
#include <utility>

namespace fei {

class LuauScriptAsset {
  private:
    std::string m_content;

  public:
    explicit LuauScriptAsset(std::string content) :
        m_content(std::move(content)) {}

    const std::string& content() const { return m_content; }
    void set_content(std::string content) { m_content = std::move(content); }
};

class LuauScriptAssetLoader : public AssetLoader<LuauScriptAsset> {
  public:
    AssetLoadResult<LuauScriptAsset>
    load(Reader& reader, const LoadContext& /*context*/) override {
        return std::make_unique<LuauScriptAsset>(reader.as_string());
    }
};

} // namespace fei
