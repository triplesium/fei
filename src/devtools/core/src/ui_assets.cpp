#include "ui_assets.hpp"

#include "asset/embed.hpp"

EMBED(devtools_index_html, "devtools/index.html");
EMBED(devtools_app_css, "devtools/app.css");
EMBED(devtools_app_js, "devtools/app.js");

namespace fei::devtools::detail {
namespace {

const Reader c_index_html = EmbededAssets::get("devtools/index.html").reader();
const Reader c_app_css = EmbededAssets::get("devtools/app.css").reader();
const Reader c_app_js = EmbededAssets::get("devtools/app.js").reader();

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

} // namespace fei::devtools::detail
