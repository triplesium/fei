#include "base/bitflags.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <sstream>

namespace {

enum class TestFlag : std::uint8_t {
    Read = 1 << 0,
    Write = 1 << 1,
    Execute = 1 << 2,
};

} // namespace

TEST_CASE("BitFlags set and unset enum flags", "[base][bitflags]") {
    BitFlags<TestFlag> flags {
        TestFlag::Read,
        TestFlag::Write,
    };

    REQUIRE(flags.is_set(TestFlag::Read));
    REQUIRE(flags.is_set(TestFlag::Write));
    REQUIRE_FALSE(flags.is_set(TestFlag::Execute));
    REQUIRE(flags);

    flags.unset(TestFlag::Write);
    REQUIRE(flags.is_set(TestFlag::Read));
    REQUIRE_FALSE(flags.is_set(TestFlag::Write));

    flags.clear();
    REQUIRE_FALSE(flags);
}

TEST_CASE("BitFlags combine and compare raw flags", "[base][bitflags]") {
    BitFlags<TestFlag> read {TestFlag::Read};
    BitFlags<TestFlag> write {TestFlag::Write};

    auto read_write = read | write;
    REQUIRE(read_write.is_set(TestFlag::Read));
    REQUIRE(read_write.is_set(TestFlag::Write));
    REQUIRE((read_write & TestFlag::Read).is_set(TestFlag::Read));
    REQUIRE_FALSE((read_write & TestFlag::Execute));

    read_write ^= TestFlag::Write;
    REQUIRE(read_write == read);
    REQUIRE(read_write != write);

    auto from_raw = BitFlags<TestFlag>::from_raw(
        static_cast<std::uint8_t>(TestFlag::Read) |
        static_cast<std::uint8_t>(TestFlag::Execute)
    );
    REQUIRE(from_raw.to_raw() == 0b00000101);
    REQUIRE(from_raw.is_set(TestFlag::Execute));
}

TEST_CASE("BitFlags stream as underlying bitsets", "[base][bitflags]") {
    std::ostringstream stream;
    stream << BitFlags<TestFlag> {TestFlag::Read};

    REQUIRE(stream.str() == "00000001");
}
