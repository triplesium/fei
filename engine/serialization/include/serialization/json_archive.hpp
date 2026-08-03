#pragma once

#include "base/result.hpp"
#include "serialization/node.hpp"

#include <string>
#include <string_view>

namespace fei::serialization {

struct JsonError {
    enum class Kind {
        Parse,
        Unsupported,
    };

    Kind kind;
    std::string message;
};

Result<SerializedNode, JsonError> read_json(std::string_view text);

Result<std::string, JsonError>
write_json(const SerializedNode& node, int indent = 4);

} // namespace fei::serialization
