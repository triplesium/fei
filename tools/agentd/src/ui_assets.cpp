#include "ui_assets.hpp"

#include "asset/embed.hpp"

EMBED(agentd_index_html, "agentd/index.html");
EMBED(agentd_app_css, "agentd/app.css");
EMBED(agentd_app_js, "agentd/app.js");

namespace ets::agentd::detail {
namespace {

const Reader c_index_html = EmbeddedAssets::get("agentd/index.html").reader();
const Reader c_app_css = EmbeddedAssets::get("agentd/app.css").reader();
const Reader c_app_js = EmbeddedAssets::get("agentd/app.js").reader();

} // namespace

Optional<UiAsset> find_ui_asset(std::string_view path) {
    if (path == "/ui/" || path == "/ui/index.html") {
        return UiAsset {
            .content = c_index_html.as_string_view(),
            .content_type = "text/html; charset=utf-8",
        };
    }
    if (path == "/ui/app.css") {
        return UiAsset {
            .content = c_app_css.as_string_view(),
            .content_type = "text/css; charset=utf-8",
        };
    }
    if (path == "/ui/app.js") {
        return UiAsset {
            .content = c_app_js.as_string_view(),
            .content_type = "text/javascript; charset=utf-8",
        };
    }
    return nullopt;
}

} // namespace ets::agentd::detail
