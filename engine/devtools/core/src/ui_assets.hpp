#pragma once

#include "base/optional.hpp"

#include <string_view>

namespace fei::devtools::detail {

struct UiAsset {
    std::string_view content;
    std::string_view content_type;
};

inline constexpr std::string_view c_discovery_json {
    R"({"name":"fei-devtools","version":1,"manifest":"/api/v1/manifest","schemas":"/api/v1/schemas","status":"/api/v1/status","ui":"/ui/"})"
};

inline constexpr std::string_view c_ui_content_security_policy {
    "default-src 'self'; base-uri 'none'; connect-src 'self'; "
    "font-src 'none'; frame-ancestors 'none'; img-src 'self' blob:; "
    "object-src 'none'; script-src 'self'; style-src 'self'"
};

Optional<UiAsset> find_ui_asset(std::string_view path);

} // namespace fei::devtools::detail
