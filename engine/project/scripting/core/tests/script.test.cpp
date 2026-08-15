#include "project_scripting/script.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE(
    "Project scripts match language extensions case insensitively",
    "[project][scripting]"
) {
    CHECK(
        project_scripting::script_path_has_extension(
            AssetReference {.fallback_path = "project://scripts/main.lua"},
            ".lua"
        )
    );
    CHECK(
        project_scripting::script_path_has_extension(
            AssetReference {.fallback_path = "project://scripts/main.LUAU"},
            ".luau"
        )
    );
    CHECK_FALSE(
        project_scripting::script_path_has_extension(
            AssetReference {.fallback_path = "project://scripts/main.luau"},
            ".lua"
        )
    );
}
