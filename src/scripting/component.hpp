#pragma once
#include "asset/handle.hpp"
#include "scripting/asset.hpp"

namespace fei {

struct ScriptComponent {
    Handle<ScriptAsset> script;
};

} // namespace fei
