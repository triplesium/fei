#include "ui_assets.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei::agentd::detail;

TEST_CASE("Agentd embeds its playtest observability UI", "[agentd][ui]") {
    const auto index = find_ui_asset("/ui/");
    REQUIRE(index);
    CHECK(index->content_type == "text/html; charset=utf-8");
    CHECK(index->content.find("Fei Playtest") != std::string_view::npos);
    CHECK(index->content.find(R"(id="game-frame")") != std::string_view::npos);
    CHECK(index->content.find(R"(id="timeline")") != std::string_view::npos);

    const auto styles = find_ui_asset("/ui/app.css");
    REQUIRE(styles);
    CHECK(styles->content_type == "text/css; charset=utf-8");
    CHECK(styles->content.find(".frame-stage") != std::string_view::npos);
    CHECK(styles->content.find(".event-row") != std::string_view::npos);

    const auto script = find_ui_asset("/ui/app.js");
    REQUIRE(script);
    CHECK(script->content_type == "text/javascript; charset=utf-8");
    CHECK(
        script->content.find("/api/v1/play/events") != std::string_view::npos
    );
    CHECK(script->content.find("/api/v1/play/frame") != std::string_view::npos);
    CHECK(script->content.find("innerHTML") == std::string_view::npos);
    CHECK_FALSE(find_ui_asset("/ui/missing.js"));

    CHECK(
        c_ui_content_security_policy.find("connect-src 'self'") !=
        std::string_view::npos
    );
    CHECK(
        c_ui_content_security_policy.find("frame-ancestors 'none'") !=
        std::string_view::npos
    );
}
