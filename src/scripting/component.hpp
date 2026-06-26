#pragma once
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "base/optional.hpp"
#include "scripting/asset.hpp"
#include "scripting/runtime.hpp"

namespace fei {

struct ScriptComponent {
    Handle<ScriptAsset> script;
    Optional<ScriptModuleId> module;
    AssetId loaded_script {invalid_asset_id};
};

} // namespace fei
