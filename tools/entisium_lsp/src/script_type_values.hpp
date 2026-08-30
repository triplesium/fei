#pragma once

#include "Plugin/PluginTypes.hpp"

#include <string_view>
#include <vector>

namespace ets::lsp {

[[nodiscard]] std::vector<Luau::LanguageServer::Plugin::TextEdit>
script_type_value_edits(std::string_view source);

} // namespace ets::lsp
