#include "base/env.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

using namespace fei;

namespace {

void set_test_env(char const* name, char const* value) {
#if defined(_WIN32)
    REQUIRE(_putenv_s(name, value) == 0);
#else
    REQUIRE(setenv(name, value, 1) == 0);
#endif
}

void unset_test_env(char const* name) {
#if defined(_WIN32)
    REQUIRE(_putenv_s(name, "") == 0);
#else
    REQUIRE(unsetenv(name) == 0);
#endif
}

} // namespace

TEST_CASE("Environment variables can be read and parsed", "[base][env]") {
    constexpr char name[] = "FEI_TEST_ENV_VALUE";

    set_test_env(name, "42");
    REQUIRE(
        read_environment_variable(name) == std::optional<std::string>("42")
    );
    REQUIRE(read_environment_variable<int>(name) == std::optional<int>(42));

    set_test_env(name, "bad");
    REQUIRE_FALSE(read_environment_variable<int>(name));

    unset_test_env(name);
}
