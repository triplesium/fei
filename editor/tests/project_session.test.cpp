#include "editor/project_session.hpp"

#include "app/app.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;
using namespace fei::editor;

TEST_CASE(
    "Editor project sessions require a project resource",
    "[editor][project]"
) {
    App app;
    EditorProjectSession session;

    const auto status = session.open(app, false);

    REQUIRE_FALSE(status);
    CHECK_FALSE(session.is_open());
    CHECK(status.error() == "Project resource is not available");
}
