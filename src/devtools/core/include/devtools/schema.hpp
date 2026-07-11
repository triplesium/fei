#pragma once

#include "base/result.hpp"
#include "refl/type.hpp"

#include <string>
#include <vector>

namespace fei::devtools {

Result<std::string, std::string>
build_schema_json(const std::vector<TypeId>& roots);

} // namespace fei::devtools
