#include "scripting/asset_compiler.hpp"

#include "asset/server.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace ets {

Result<LuauScriptSource, LuauScriptError>
LuauAssetCompiler::source(Handle<LuauScriptAsset> asset) const {
    const auto script = m_assets.get(asset);
    if (!script) {
        std::string message = "Luau script asset failed to load";
        if (const auto error = m_assets.load_error(asset)) {
            message += ": " + error->message;
        }
        return failure(LuauScriptError {std::move(message)});
    }
    const auto path = m_assets.path(asset);
    return LuauScriptSource {
        .name = path ?
                    path->as_string() :
                    "luau_script_asset_" + std::to_string(asset.id()) + ".luau",
        .content = script->content(),
    };
}

Result<std::shared_ptr<const LuauModuleMetadata>, LuauScriptError>
LuauAssetCompiler::resolve_imported_metadata(
    const LuauScriptAsset& importer,
    std::string_view specifier
) const {
    const auto imported = std::ranges::find(
        importer.imports(),
        specifier,
        &LuauScriptImport::specifier
    );
    if (imported == importer.imports().end()) {
        return failure(
            LuauScriptError {
                "Luau imported system module '" + std::string(specifier) +
                    "' is not a relative script import",
            }
        );
    }
    const auto asset = m_asset_server.load<LuauScriptAsset>(imported->path);
    return module_metadata(asset);
}

Result<std::shared_ptr<const LuauModuleMetadata>, LuauScriptError>
LuauAssetCompiler::module_metadata(Handle<LuauScriptAsset> asset) const {
    if (const auto cached = m_metadata.find(asset.id());
        cached != m_metadata.end()) {
        return cached->second;
    }
    auto script_source = source(asset);
    if (!script_source) {
        return failure(std::move(script_source.error()));
    }
    auto metadata = compile_luau_module_metadata(*script_source);
    if (!metadata) {
        return failure(std::move(metadata.error()));
    }
    auto shared =
        std::make_shared<const LuauModuleMetadata>(std::move(*metadata));
    m_metadata.emplace(asset.id(), shared);
    return shared;
}

Result<std::shared_ptr<const LuauScriptModuleArtifact>, LuauScriptError>
LuauAssetCompiler::compile_module(Handle<LuauScriptAsset> asset) const {
    if (const auto cached = m_modules.find(asset.id());
        cached != m_modules.end()) {
        return cached->second;
    }
    const auto script = m_assets.get(asset);
    if (!script) {
        auto missing = source(asset);
        return failure(std::move(missing.error()));
    }
    auto script_source = source(asset);
    if (!script_source) {
        return failure(std::move(script_source.error()));
    }
    auto metadata = module_metadata(asset);
    if (!metadata) {
        return failure(std::move(metadata.error()));
    }
    std::unordered_map<std::string, std::shared_ptr<const LuauModuleMetadata>>
        imported_metadata;
    for (const auto& import : script->imports()) {
        auto imported = resolve_imported_metadata(*script, import.specifier);
        if (!imported) {
            return failure(std::move(imported.error()));
        }
        imported_metadata.emplace(import.specifier, std::move(*imported));
    }
    auto compiled = compile_luau_script_module(
        *script_source,
        LuauCompileOptions {
            .metadata = std::move(*metadata),
            .module_metadata_resolver =
                [imported_metadata =
                     std::move(imported_metadata)](std::string_view specifier)
                -> Result<
                    std::shared_ptr<const LuauModuleMetadata>,
                    LuauScriptError> {
                const auto found =
                    imported_metadata.find(std::string(specifier));
                if (found == imported_metadata.end()) {
                    return failure(
                        LuauScriptError {
                            "Luau imported system module '" +
                                std::string(specifier) + "' was not resolved",
                        }
                    );
                }
                return found->second;
            },
        }
    );
    if (!compiled) {
        return failure(std::move(compiled.error()));
    }
    auto shared =
        std::make_shared<const LuauScriptModuleArtifact>(std::move(*compiled));
    m_modules.emplace(asset.id(), shared);
    return shared;
}

} // namespace ets
