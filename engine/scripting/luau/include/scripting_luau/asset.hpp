#pragma once

#include "asset/loader.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fei {

struct LuauScriptImport {
    std::string specifier;
    AssetPath path;
};

class LuauScriptAsset {
  private:
    std::string m_content;
    std::vector<LuauScriptImport> m_imports;

  public:
    explicit LuauScriptAsset(
        std::string content,
        std::vector<LuauScriptImport> imports = {}
    ) : m_content(std::move(content)), m_imports(std::move(imports)) {}

    const std::string& content() const { return m_content; }
    const std::vector<LuauScriptImport>& imports() const { return m_imports; }
    void set_content(
        std::string content,
        std::vector<LuauScriptImport> imports = {}
    ) {
        m_content = std::move(content);
        m_imports = std::move(imports);
    }
};

class LuauScriptAssetLoader : public AssetLoader<LuauScriptAsset> {
  public:
    AssetLoadResult<LuauScriptAsset>
    load(Reader& reader, const LoadContext& context) override;
};

} // namespace fei
