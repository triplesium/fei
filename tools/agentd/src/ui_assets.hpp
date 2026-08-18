#pragma once

#include "base/optional.hpp"

#include <string_view>

namespace fei::agentd::detail {

struct UiAsset {
    std::string_view content;
    std::string_view content_type;
};

inline constexpr std::string_view c_ui_content_security_policy {
    "default-src 'self'; base-uri 'none'; connect-src 'self'; "
    "font-src 'none'; frame-ancestors 'none'; img-src 'self' blob:; "
    "object-src 'none'; script-src 'self'; style-src 'self'"
};

Optional<UiAsset> find_ui_asset(std::string_view path);

} // namespace fei::agentd::detail
