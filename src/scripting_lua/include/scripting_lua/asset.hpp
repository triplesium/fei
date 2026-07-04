#pragma once
#include "asset/loader.hpp"

#include <string>
#include <utility>

namespace fei {

class LuaScriptAsset {
  private:
    std::string m_content;

  public:
    LuaScriptAsset(std::string content) : m_content(std::move(content)) {}

    const std::string& content() const { return m_content; }
    void set_content(std::string content) { m_content = std::move(content); }
};

class LuaScriptAssetLoader : public AssetLoader<LuaScriptAsset> {
  public:
    AssetLoadResult<LuaScriptAsset>
    load(Reader& reader, const LoadContext& /*context*/) override {
        return std::make_unique<LuaScriptAsset>(reader.as_string());
    }
};

} // namespace fei
