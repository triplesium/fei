#pragma once

#include <string>
#include <string_view>

namespace ets::detail::luau_compiler {

std::string module_name_from_source(std::string_view source_name);

} // namespace ets::detail::luau_compiler
