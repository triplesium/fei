#include "scripting_luau/asset.hpp"

#include "scripting_luau/compiler.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fei {
namespace {

Result<AssetPath, AssetLoadError>
resolve_luau_import(const LoadContext& context, const std::string& specifier) {
    auto path = context.asset_path().resolve_embed_str(specifier);
    if (path.path().extension().empty()) {
        auto with_extension = path.path();
        with_extension += ".luau";
        const auto source = path.source();
        path = AssetPath(with_extension);
        if (source) {
            path = path.with_source(*source);
        }
    }
    if (path.path().extension() != ".luau") {
        return failure(AssetLoadError(
            context.asset_path(),
            "Luau import '" + specifier + "' must reference a .luau file"
        ));
    }
    if (path.is_unapproved()) {
        return failure(AssetLoadError(
            context.asset_path(),
            "Luau import '" + specifier + "' escapes its asset source"
        ));
    }
    if (path.source() != context.asset_path().source()) {
        return failure(AssetLoadError(
            context.asset_path(),
            "Luau import '" + specifier +
                "' references a different asset source"
        ));
    }
    return path;
}

} // namespace

AssetLoadResult<LuauScriptAsset>
LuauScriptAssetLoader::load(Reader& reader, const LoadContext& context) {
    std::string content = reader.as_string();
    auto specifiers = extract_luau_script_imports(
        ScriptSource {
            .name = context.asset_path().as_string(),
            .content = content,
        }
    );
    if (!specifiers) {
        return failure(AssetLoadError(
            context.asset_path(),
            std::move(specifiers.error().message)
        ));
    }

    std::vector<LuauScriptImport> imports;
    imports.reserve(specifiers->size());
    for (auto& specifier : *specifiers) {
        auto path = resolve_luau_import(context, specifier);
        if (!path) {
            return failure(std::move(path.error()));
        }
        imports.push_back(
            LuauScriptImport {
                .specifier = std::move(specifier),
                .path = std::move(*path),
            }
        );
    }
    return std::make_unique<LuauScriptAsset>(
        std::move(content),
        std::move(imports)
    );
}

} // namespace fei
